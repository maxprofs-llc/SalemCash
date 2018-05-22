\// Copyright (c) 2018 The SalemCash developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef SALEMCASH_SALEMCASHCONSENSUS_H
#define SALEMCASH_SALEMCASHCONSENSUS_H

#include <stdint.h>

#if defined(BUILD_SALEMCASH_INTERNAL) && defined(HAVE_CONFIG_H)
#include <config/salemcash-config.h>
  #if defined(_WIN32)
    #if defined(DLL_EXPORT)
      #if defined(HAVE_FUNC_ATTRIBUTE_DLLEXPORT)
        #define EXPORT_SYMBOL __declspec(dllexport)
      #else
        #define EXPORT_SYMBOL
      #endif
    #endif
  #elif defined(HAVE_FUNC_ATTRIBUTE_VISIBILITY)
    #define EXPORT_SYMBOL __attribute__ ((visibility ("default")))
  #endif
#elif defined(MSC_VER) && !defined(STATIC_LIBSALEMCASHCONSENSUS)
  #define EXPORT_SYMBOL __declspec(dllimport)
#endif

#ifndef EXPORT_SYMBOL
  #define EXPORT_SYMBOL
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define SALEMCASHCONSENSUS_API_VER 1

typedef enum salemcashconsensus_error_t
{
    SALEMCASHCONSENSUS_ERR_OK = 0,
    SALEMCASHCONSENSUS_ERR_TX_INDEX,
    SALEMCASHCONSENSUS_ERR_TX_SIZE_MISMATCH,
    SALEMCASHCONSENSUS_ERR_TX_DESERIALIZE,
    SALEMCASHCONSENSUS_ERR_AMOUNT_REQUIRED,
    SALEMCASHCONSENSUS_ERR_INVALID_FLAGS,
} salemcashconsensus_error;

/** Script verification flags */
enum
{
    SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_NONE                = 0,
    SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_P2SH                = (1U << 0), // evaluate P2SH (BIP16) subscripts
    SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_DERSIG              = (1U << 2), // enforce strict DER (BIP66) compliance
    SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_NULLDUMMY           = (1U << 4), // enforce NULLDUMMY (BIP147)
    SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_CHECKLOCKTIMEVERIFY = (1U << 9), // enable CHECKLOCKTIMEVERIFY (BIP65)
    SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_CHECKSEQUENCEVERIFY = (1U << 10), // enable CHECKSEQUENCEVERIFY (BIP112)
    SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_WITNESS             = (1U << 11), // enable WITNESS (BIP141)
    SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_ALL                 = SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_P2SH | SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_DERSIG |
                                                               SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_NULLDUMMY | SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_CHECKLOCKTIMEVERIFY |
                                                               SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_CHECKSEQUENCEVERIFY | SALEMCASHCONSENSUS_SCRIPT_FLAGS_VERIFY_WITNESS
};

/// Returns 1 if the input nIn of the serialized transaction pointed to by
/// txTo correctly spends the scriptPubKey pointed to by scriptPubKey under
/// the additional constraints specified by flags.
/// If not nullptr, err will contain an error/success code for the operation
EXPORT_SYMBOL int salemcashconsensus_verify_script(const unsigned char *scriptPubKey, unsigned int scriptPubKeyLen,
                                                 const unsigned char *txTo        , unsigned int txToLen,
                                                 unsigned int nIn, unsigned int flags, salemcashconsensus_error* err);

EXPORT_SYMBOL int salemcashconsensus_verify_script_with_amount(const unsigned char *scriptPubKey, unsigned int scriptPubKeyLen, int64_t amount,
                                    const unsigned char *txTo        , unsigned int txToLen,
                                    unsigned int nIn, unsigned int flags, salemcashconsensus_error* err);

EXPORT_SYMBOL unsigned int salemcashconsensus_version();

#ifdef __cplusplus
} // extern "C"
#endif

#undef EXPORT_SYMBOL

#endif // SALEMCASH_SALEMCASHCONSENSUS_H
