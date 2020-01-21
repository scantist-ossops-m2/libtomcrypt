/* LibTomCrypt, modular cryptographic library -- Tom St Denis
 *
 * LibTomCrypt is a library that provides various cryptographic
 * algorithms in a highly modular and flexible manner.
 *
 * The library is free for all purposes without any express
 * guarantee it works.
 */

/**
  @file openssh-privkey.c
  OpenSSH Private Key decryption demo, Steffen Jaeckel

  The basic format of the key is described here:
  https://github.com/openssh/openssh-portable/blob/master/PROTOCOL.key
*/

#define _GNU_SOURCE

#include <bsd/string.h>
#include <tomcrypt_private.h>
#include <stdarg.h>

static int verbose = 0;

static void print_hex(const char* what, const void* v, const unsigned long l)
{
  const unsigned char* p = v;
  unsigned long x, y = 0, z;

  if (!verbose) return;

  fprintf(stderr, "%s contents: \n", what);
  for (x = 0; x < l; ) {
      fprintf(stderr, "%02X ", p[x]);
      if (!(++x % 16) || x == l) {
         if((x % 16) != 0) {
            z = 16 - (x % 16);
            if(z >= 8)
               fprintf(stderr, " ");
            for (; z != 0; --z) {
               fprintf(stderr, "   ");
            }
         }
         fprintf(stderr, " | ");
         for(; y < x; y++) {
            if((y % 8) == 0)
               fprintf(stderr, " ");
            if(isgraph(p[y]))
               fprintf(stderr, "%c", p[y]);
            else
               fprintf(stderr, ".");
         }
         fprintf(stderr, "\n");
      }
      else if((x % 8) == 0) {
         fprintf(stderr, " ");
      }
  }
}

static void print_err(const char *fmt, ...)
{
   va_list args;

   if (!verbose) return;

   va_start(args, fmt);
   vfprintf(stderr, fmt, args);
}

static void die_(int err, int line)
{
   verbose = 1;
   print_err("%3d: LTC sez %s\n", line, error_to_string(err));
   exit(EXIT_FAILURE);
}

#define die(i) do { die_(i, __LINE__); } while(0)
#define DIE(s, ...) do { verbose = 1; print_err("%3d: " s "\n", __LINE__, ##__VA_ARGS__); exit(EXIT_FAILURE); } while(0)


static void check_padding(const unsigned char *p, unsigned long len)
{
   unsigned char pad = 0x1u;
   while (len != 0) {
      if (*p != pad) DIE("pad wrong 0x%02x != 0x%02x", *p, pad);
      p++;
      pad++;
      len--;
   }
}

typedef struct pka_key_ {
   enum ltc_oid_id id;
   union {
      curve25519_key ed25519;
      ecc_key ecdsa;
      rsa_key rsa;
   } u;
} pka_key;

enum blockcipher_mode {
   none, cbc, ctr, stream, gcm
};
struct ssh_blockcipher {
   const char *name;
   const char *algo;
   int len;
   enum blockcipher_mode mode;
};

/* Table as of
 * https://www.iana.org/assignments/ssh-parameters/ssh-parameters.xhtml#ssh-parameters-17
 */
const struct ssh_blockcipher ssh_ciphers[] =
{
   { "none", "", 0, none },
   { "aes256-cbc", "aes", 256 / 8, cbc },
   { 0 },
};

struct kdf_options {
   const char *name;
   const struct ssh_blockcipher *cipher;
   unsigned char salt[64];
   ulong32 saltlen;
   ulong32 num_rounds;
   const char *pass;
   unsigned long passlen;
};

int ssh_find_init_ecc(const char *pka, pka_key *key)
{
   int err;
   const char* prefix = "ecdsa-sha2-";
   size_t prefixlen = strlen(prefix);
   const ltc_ecc_curve *cu;
   if (strstr(pka, prefix) == NULL) return CRYPT_PK_INVALID_TYPE;
   if ((err = ecc_find_curve(pka + prefixlen, &cu)) != CRYPT_OK) return err;
   return ecc_set_curve(cu, &key->u.ecdsa);
}

int ssh_decode_ecdsa(const unsigned char *in, unsigned long *inlen, pka_key *key)
{
   int err;
   unsigned char groupname[64], group[512], privkey[512];
   unsigned long groupnamelen = sizeof(groupname), grouplen = sizeof(group), privkeylen = sizeof(privkey);

   if ((err = ssh_decode_sequence_multi(in, inlen,
                                        LTC_SSHDATA_STRING, groupname, &groupnamelen,
                                        LTC_SSHDATA_STRING, group, &grouplen,
                                        LTC_SSHDATA_STRING, privkey, &privkeylen,
                                        LTC_SSHDATA_EOL)) != CRYPT_OK) {
      die(err);
   }

   if ((err = ecc_set_key(privkey, privkeylen, PK_PRIVATE, &key->u.ecdsa)) != CRYPT_OK) {
      die(err);
   }

   key->id = PKA_EC;

   zeromem(groupname, sizeof(groupname));
   zeromem(group, sizeof(group));
   zeromem(privkey, sizeof(privkey));

   return err;
}

int ssh_decode_ed25519(const unsigned char *in, unsigned long *inlen, pka_key *key)
{
   int err;
   unsigned char pubkey[2048], privkey[2048];
   unsigned long pubkeylen = sizeof(pubkey), privkeylen = sizeof(privkey);

   if ((err = ssh_decode_sequence_multi(in, inlen,
                                        LTC_SSHDATA_STRING, pubkey, &pubkeylen,
                                        LTC_SSHDATA_STRING, privkey, &privkeylen,
                                        LTC_SSHDATA_EOL)) != CRYPT_OK) {
      die(err);
   }

   if ((err = ed25519_import_raw(&privkey[32], 32, PK_PRIVATE, &key->u.ed25519)) != CRYPT_OK) {
      die(err);
   }

   key->id = PKA_ED25519;

   zeromem(pubkey, sizeof(pubkey));
   zeromem(privkey, sizeof(privkey));

   return err;
}

int ssh_decode_rsa(const unsigned char *in, unsigned long *inlen, pka_key *key)
{
   int err;
   void *tmp1, *tmp2;
   if ((err = mp_init_multi(&tmp1, &tmp2, NULL)) != CRYPT_OK) {
      die(err);
   }
   if ((err = rsa_init(&key->u.rsa)) != CRYPT_OK) {
      die(err);
   }

   if ((err = ssh_decode_sequence_multi(in, inlen,
                                        LTC_SSHDATA_MPINT, key->u.rsa.N,
                                        LTC_SSHDATA_MPINT, key->u.rsa.e,
                                        LTC_SSHDATA_MPINT, key->u.rsa.d,
                                        LTC_SSHDATA_MPINT, key->u.rsa.qP,
                                        LTC_SSHDATA_MPINT, key->u.rsa.q,
                                        LTC_SSHDATA_MPINT, key->u.rsa.p,
                                        LTC_SSHDATA_EOL)) != CRYPT_OK) {
      die(err);
   }

   if ((err = mp_sub_d(key->u.rsa.p, 1,  tmp1)) != CRYPT_OK)                     { die(err); } /* tmp1 = q-1 */
   if ((err = mp_sub_d(key->u.rsa.q, 1,  tmp2)) != CRYPT_OK)                     { die(err); } /* tmp2 = p-1 */
   if ((err = mp_mod( key->u.rsa.d,  tmp1,  key->u.rsa.dP)) != CRYPT_OK)         { die(err); } /* dP = d mod p-1 */
   if ((err = mp_mod( key->u.rsa.d,  tmp2,  key->u.rsa.dQ)) != CRYPT_OK)         { die(err); } /* dQ = d mod q-1 */

   key->id = PKA_RSA;

   mp_clear_multi(tmp2, tmp1, NULL);

   return err;
}

struct ssh_pka {
   const char *name;
   int (*init)(const char*, pka_key*);
   int (*decode)(const unsigned char*, unsigned long*, pka_key*);
};

struct ssh_pka ssh_pkas[] = {
                             { "ssh-ed25519", NULL,              ssh_decode_ed25519 },
                             { "ssh-rsa",     NULL,              ssh_decode_rsa },
                             { NULL,          ssh_find_init_ecc, ssh_decode_ecdsa },
};

int ssh_decode_private_key(const unsigned char *in, unsigned long *inlen, pka_key *key)
{
   int err;
   ulong32 check1, check2;
   unsigned char pka[64], comment[256];
   unsigned long pkalen = sizeof(pka), commentlen = sizeof(comment);
   unsigned long remaining, cur_len;
   const unsigned char *p;
   size_t n;

   LTC_ARGCHK(in    != NULL);
   LTC_ARGCHK(inlen != NULL);
   LTC_ARGCHK(key   != NULL);

   p = in;
   cur_len = *inlen;

   if ((err = ssh_decode_sequence_multi(p, &cur_len,
                                        LTC_SSHDATA_UINT32, &check1,
                                        LTC_SSHDATA_UINT32, &check2,
                                        LTC_SSHDATA_STRING, pka, &pkalen,
                                        LTC_SSHDATA_EOL)) != CRYPT_OK) {
      die(err);
   }
   if (check1 != check2) DIE("decrypt failed");

   p += cur_len;
   remaining = *inlen - cur_len;
   cur_len = remaining;

   for (n = 0; n < sizeof(ssh_pkas)/sizeof(ssh_pkas[0]); ++n) {
      if (ssh_pkas[n].name != NULL) {
         if (XSTRCMP((char*)pka, ssh_pkas[n].name) != 0) continue;
      } else {
         if ((ssh_pkas[n].init == NULL) ||
               (ssh_pkas[n].init((char*)pka, key) != CRYPT_OK)) continue;
      }
      if ((err = ssh_pkas[n].decode(p, &cur_len, key)) != CRYPT_OK) {
         die(err);
      }
      break;
   }
   if (n == sizeof(ssh_pkas)/sizeof(ssh_pkas[0])) DIE("unsupported pka %s", pka);

   p += cur_len;
   remaining -= cur_len;
   cur_len = remaining;

   if ((err = ssh_decode_sequence_multi(p, &cur_len,
                                        LTC_SSHDATA_STRING, comment, &commentlen,
                                        LTC_SSHDATA_EOL)) != CRYPT_OK) {
      die(err);
   }

   printf("comment: %s\n", comment);

   p += cur_len;
   remaining -= cur_len;

   check_padding(p, remaining);

   return err;
}

int ssh_decrypt_private_keys(unsigned char *in, unsigned long *inlen, struct kdf_options *opts)
{
   int err, cipher;
   unsigned char symkey[128];
   unsigned long symkey_len;
   symmetric_CBC cbc_ctx;

   LTC_ARGCHK(in    != NULL);
   LTC_ARGCHK(inlen != NULL);
   LTC_ARGCHK(opts  != NULL);

   cipher = find_cipher(opts->cipher->algo);
   symkey_len = opts->cipher->len + cipher_descriptor[cipher].block_length;

   if (sizeof(symkey) < symkey_len) DIE("too small");

   if ((err = bcrypt_pbkdf_openbsd(opts->pass, opts->passlen, opts->salt, opts->saltlen, opts->num_rounds, find_hash("sha512"), symkey, &symkey_len)) != CRYPT_OK) {
      die(err);
   }

   if ((err = cbc_start(cipher, symkey + opts->cipher->len, symkey, opts->cipher->len, 0, &cbc_ctx)) != CRYPT_OK) {
      die(err);
   }
   if ((err = cbc_decrypt(in, in, *inlen, &cbc_ctx)) != CRYPT_OK) {
      die(err);
   }
   print_hex("decrypted", in, *inlen);

   zeromem(symkey, sizeof(symkey));
   zeromem(&cbc_ctx, sizeof(cbc_ctx));

   return err;
}

int ssh_decode_header(unsigned char *in, unsigned long *inlen, struct kdf_options *opts)
{
   int err;
   unsigned char ciphername[64], kdfname[64], kdfoptions[128], pubkey1[2048];
   unsigned long ciphernamelen = sizeof(ciphername), kdfnamelen = sizeof(kdfname);
   unsigned long kdfoptionslen = sizeof(kdfoptions), pubkey1len = sizeof(pubkey1);
   ulong32 num_keys;
   size_t i;

   void *magic = strstr((const char*)in, "openssh-key-v1");
   size_t slen = strlen("openssh-key-v1");
   unsigned char *start = &in[slen + 1];
   unsigned long len = *inlen - slen - 1;

   if (magic == NULL) DIE("magic not found");
   if (magic != in) DIE("magic not at the beginning");

   if ((err = ssh_decode_sequence_multi(start, &len,
                                        LTC_SSHDATA_STRING, ciphername, &ciphernamelen,
                                        LTC_SSHDATA_STRING, kdfname, &kdfnamelen,
                                        LTC_SSHDATA_STRING, kdfoptions, &kdfoptionslen,
                                        LTC_SSHDATA_UINT32, &num_keys,
                                        LTC_SSHDATA_STRING, pubkey1, &pubkey1len,
                                        LTC_SSHDATA_EOL)) != CRYPT_OK) {
      die(err);
   }
   if (num_keys != 1) DIE("more than 1 pubkey not supported");

   print_hex("public key", pubkey1, pubkey1len);

   *inlen = len + slen + 1;

   for (i = 0; i < sizeof(ssh_ciphers)/sizeof(ssh_ciphers[0]); ++i) {
      if (XSTRCMP((char*)ciphername, ssh_ciphers[i].name) == 0) {
         opts->cipher = &ssh_ciphers[i];
         break;
      }
   }
   if (opts->cipher == NULL) DIE("can't find algo");

   if (XSTRCMP((char*)kdfname, "none") == 0) {
      /* NOP */
      opts->name = "none";
   } else if (XSTRCMP((char*)kdfname, "bcrypt") == 0) {
      opts->name = "bcrypt";
      opts->saltlen = sizeof(opts->salt);
      len = kdfoptionslen;
      if ((err = ssh_decode_sequence_multi(kdfoptions, &len,
                                           LTC_SSHDATA_STRING, opts->salt, &opts->saltlen,
                                           LTC_SSHDATA_UINT32, &opts->num_rounds,
                                           LTC_SSHDATA_EOL)) != CRYPT_OK) {
         die(err);
      }
      if (len != kdfoptionslen) DIE("unused data %lu", kdfoptionslen-len);
   } else {
      DIE("unsupported kdf %s", kdfname);
   }

   return err;
}


void read_openssh_private_key(FILE *f, char *pem, size_t *w)
{
   const char *openssh_privkey_start = "-----BEGIN OPENSSH PRIVATE KEY-----";
   const char *openssh_privkey_end = "-----END OPENSSH PRIVATE KEY-----";
   char buf[81];
   char *end;
   size_t n = 0;

   while (fgets(buf, sizeof(buf), f)) {
      const char *start = strstr(buf, openssh_privkey_start);
      if (start != NULL) {
         size_t l;
         start += strlen(openssh_privkey_start);
         l = strlcpy(pem + n, start, *w - n);
         n += l;
         break;
      }
   }
   while (fgets(buf, sizeof(buf), f)) {
      size_t l = strlcpy(pem + n, buf, *w - n);
      if (l == 0) {
         DIE("strlcpy sez no");
      }
      n += l;
   }
   end = strstr(pem, openssh_privkey_end);
   if (end == NULL) DIE("could not find PEM end-tag");
   *end = '\0';
   *w = end - pem;
}

int main(int argc, char **argv)
{
   int err;

   char pem[100 * 72];
   size_t plen = sizeof(pem);
   FILE *f = NULL;
   unsigned char b64_decoded[sizeof(pem)], privkey[sizeof(pem)];
   unsigned long b64_decoded_len = sizeof(pem), privkey_len = sizeof(privkey);
   struct kdf_options opts;
   unsigned char *p;
   unsigned long len;
   pka_key k;

   if ((err = register_all_ciphers()) != CRYPT_OK) {
      die(err);
   }
   if ((err = register_all_hashes()) != CRYPT_OK) {
      die(err);
   }
   if ((err = crypt_mp_init("ltm")) != CRYPT_OK) {
      die(err);
   }

   if (argc > 1) f = fopen(argv[1], "r");
   else f = stdin;
   if (f == NULL) DIE("fopen sez no");

   read_openssh_private_key(f, pem, &plen);

   if ((err = base64_sane_decode(pem, plen, b64_decoded, &b64_decoded_len)) != CRYPT_OK) {
      die(err);
   }

   p = b64_decoded;
   plen = b64_decoded_len;
   len = plen;

   print_hex("decoded", p, plen);

   if ((err = ssh_decode_header(p, &len, &opts)) != CRYPT_OK) {
      die(err);
   }
   p += len;
   plen -= len;
   len = plen;

   print_hex("remaining", p, plen);

   if ((err = ssh_decode_sequence_multi(p, &len,
                                        LTC_SSHDATA_STRING, privkey, &privkey_len,
                                        LTC_SSHDATA_EOL)) != CRYPT_OK) {
      die(err);
   }
   p += len;
   plen -= len;

   opts.pass = "abc123";
   opts.passlen = 6;

   if (XSTRCMP(opts.name, "none") != 0) {
      len = privkey_len;
      if ((err = ssh_decrypt_private_keys(privkey, &len, &opts)) != CRYPT_OK) {
         die(err);
      }
   }

   len = privkey_len;
   if ((err = ssh_decode_private_key(privkey, &len, &k)) != CRYPT_OK) {
      die(err);
   }

   return EXIT_SUCCESS;
}

/* ref:         $Format:%D$ */
/* git commit:  $Format:%H$ */
/* commit time: $Format:%ai$ */
