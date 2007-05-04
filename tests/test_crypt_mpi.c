#include "CUnit/Basic.h"

#include "tests.h"
#include "openpgpsdk/types.h"
#include "openpgpsdk/keyring.h"
#include "../src/advanced/keyring_local.h"
#include "openpgpsdk/packet.h"
#include "openpgpsdk/create.h"

static char secring[MAXBUF+1];
static char pubring[MAXBUF+1];
static ops_keyring_t pub_keyring;
static ops_keyring_t sec_keyring;
static const ops_key_data_t *pubkey;
static const ops_key_data_t *seckey;

int init_suite_crypt_mpi(void)
    {
    static char keydetails[MAXBUF+1];
    int fd=0;
    char cmd[MAXBUF+1];
    char *rsa_nopass="Key-Type: RSA\nKey-Usage: encrypt, sign\nName-Real: Alpha\nName-Comment: RSA, no passphrase\nName-Email: alpha@test.com\nKey-Length: 1024\n";
    
    // Create temp directory
    if (!mktmpdir())
	return 1;

    /*
     * Create a RSA keypair with no passphrase
     */

    snprintf(keydetails,MAXBUF,"%s/%s",dir,"keydetails.alpha");

    if ((fd=open(keydetails,O_WRONLY | O_CREAT | O_EXCL, 0600))<0)
	{
	fprintf(stderr,"Can't create key details\n");
	return 1;
	}

    write(fd,rsa_nopass,strlen(rsa_nopass));
    close(fd);

    snprintf(cmd,MAXBUF,"gpg --quiet --gen-key --expert --homedir=%s --batch %s",dir,keydetails);
    system(cmd);

    // Initialise OPS 
    ops_init();

    // read keyrings
    snprintf(pubring,MAXBUF,"%s/pubring.gpg", dir);
    ops_keyring_read(&pub_keyring,pubring);

    snprintf(secring,MAXBUF,"%s/secring.gpg", dir);
    ops_keyring_read(&sec_keyring,secring);

    char keyid[]="Alpha (RSA, no passphrase) <alpha@test.com>";
    pubkey=ops_keyring_find_key_by_userid(&pub_keyring,keyid);
    seckey=ops_keyring_find_key_by_userid(&sec_keyring,keyid);

    // Return success
    return 0;
    }

int clean_suite_crypt_mpi(void)
    {
    char cmd[MAXBUF+1];
	
    /* Close OPS */
    
    ops_keyring_free(&pub_keyring);
    ops_keyring_free(&sec_keyring);
    ops_finish();

    /* Remove test dir and files */
    snprintf(cmd,MAXBUF,"rm -rf %s", dir);
    if (system(cmd))
	{
	perror("Can't delete test directory ");
	return 1;
	}
    
    return 0;
    }

void test_crypt_mpi(void)
    {
#define BUFSZ 1024
    unsigned char buf[]="hello world";
    unsigned char out[BUFSZ];

    ops_boolean_t rtn;
    
    ops_pk_session_key_t *session_key=ops_create_pk_session_key(pubkey);
    // the encrypted_mpi is now in session_key->parameters.rsa.encrypted_m

    // decrypt it
    //    BIGNUM *enc_m;
    rtn=ops_decrypt_mpi(out,BUFSZ, session_key->parameters.rsa.encrypted_m, &seckey->key.skey);

    // is it the same?
    int n=strncmp((char *)buf,(char *)out,sizeof(buf));
    printf ("n=%d\n",n);
    }

CU_pSuite suite_crypt_mpi()
    {
    CU_pSuite suite = NULL;

    suite = CU_add_suite("Crypt MPI Suite", init_suite_crypt_mpi, clean_suite_crypt_mpi);
    if (!suite)
	    return NULL;

    // add tests to suite
    
    if (NULL == CU_add_test(suite, "encrypt_mpi, then decrypt_mpi", test_crypt_mpi))
	    return NULL;
    
    return suite;
    }