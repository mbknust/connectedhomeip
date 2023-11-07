/*
 *
 *    Copyright (c) 2022 Project CHIP Authors
 *    All rights reserved.
 *
 *    Licensed under the Apache License, Version 2.0 (the "License");
 *    you may not use this file except in compliance with the License.
 *    You may obtain a copy of the License at
 *
 *        http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Unless required by applicable law or agreed to in writing, software
 *    distributed under the License is distributed on an "AS IS" BASIS,
 *    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *    See the License for the specific language governing permissions and
 *    limitations under the License.
 */

#include <inttypes.h>

#include <crypto/PSAOperationalKeystore.h>
#include <gtest/gtest.h>
#include <lib/support/CHIPMem.h>
#include <lib/support/CodeUtils.h>
#include <lib/support/Span.h>
#include <lib/support/UnitTestExtendedAssertions.h>

using namespace chip;
using namespace chip::Crypto;

namespace {
class TestPSAOpKeyStore : public ::testing::Test
{
public:
    static void SetUpTestSuite()
    {
        VerifyOrDie(chip::Platform::MemoryInit() == CHIP_NO_ERROR);
        add_entropy_source(test_entropy_source, nullptr, 16);
#if CHIP_CRYPTO_PSA
        psa_crypto_init();
#endif
    }
    static void TearDownTestSuite() { chip::Platform::MemoryShutdown(); }
};

TEST_F(TestPSAOpKeyStore, TestBasicLifeCycle)
{
    PSAOperationalKeystore opKeystore;

    FabricIndex kFabricIndex    = 111;
    FabricIndex kBadFabricIndex = static_cast<FabricIndex>(kFabricIndex + 10u);

    // Can generate a key and get a CSR
    uint8_t csrBuf[kMIN_CSR_Buffer_Size];
    MutableByteSpan csrSpan{ csrBuf };
    CHIP_ERROR err = opKeystore.NewOpKeypairForFabric(kFabricIndex, csrSpan);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(opKeystore.HasPendingOpKeypair() == true);
    EXPECT_TRUE(opKeystore.HasOpKeypairForFabric(kFabricIndex) == false);

    P256PublicKey csrPublicKey1;
    err = VerifyCertificateSigningRequest(csrSpan.data(), csrSpan.size(), csrPublicKey1);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Can regenerate a second CSR and it has different PK
    csrSpan = MutableByteSpan{ csrBuf };
    err     = opKeystore.NewOpKeypairForFabric(kFabricIndex, csrSpan);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(opKeystore.HasPendingOpKeypair() == true);

    P256PublicKey csrPublicKey2;
    err = VerifyCertificateSigningRequest(csrSpan.data(), csrSpan.size(), csrPublicKey2);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(!csrPublicKey1.Matches(csrPublicKey2));

    // Cannot NewOpKeypair for a different fabric if one already pending
    uint8_t badCsrBuf[kMIN_CSR_Buffer_Size];
    MutableByteSpan badCsrSpan{ badCsrBuf };
    err = opKeystore.NewOpKeypairForFabric(kBadFabricIndex, badCsrSpan);
    EXPECT_TRUE(err == CHIP_ERROR_INVALID_FABRIC_INDEX);
    EXPECT_TRUE(opKeystore.HasPendingOpKeypair() == true);

    // Fail to generate CSR for invalid fabrics
    csrSpan = MutableByteSpan{ csrBuf };
    err     = opKeystore.NewOpKeypairForFabric(kUndefinedFabricIndex, csrSpan);
    EXPECT_TRUE(err == CHIP_ERROR_INVALID_FABRIC_INDEX);

    csrSpan = MutableByteSpan{ csrBuf };
    err     = opKeystore.NewOpKeypairForFabric(kMaxValidFabricIndex + 1, csrSpan);
    EXPECT_TRUE(err == CHIP_ERROR_INVALID_FABRIC_INDEX);

    // No storage done by NewOpKeypairForFabric
    EXPECT_TRUE(opKeystore.HasOpKeypairForFabric(kFabricIndex) == false);

    // Even after error, the previous valid pending keypair stays valid.
    EXPECT_TRUE(opKeystore.HasPendingOpKeypair() == true);

    // Activating with mismatching fabricIndex and matching public key fails
    err = opKeystore.ActivateOpKeypairForFabric(kBadFabricIndex, csrPublicKey2);
    EXPECT_TRUE(err == CHIP_ERROR_INVALID_FABRIC_INDEX);
    EXPECT_TRUE(opKeystore.HasPendingOpKeypair() == true);
    EXPECT_TRUE(opKeystore.HasOpKeypairForFabric(kFabricIndex) == false);

    // Activating with matching fabricIndex and mismatching public key fails
    err = opKeystore.ActivateOpKeypairForFabric(kFabricIndex, csrPublicKey1);
    EXPECT_TRUE(err == CHIP_ERROR_INVALID_PUBLIC_KEY);
    EXPECT_TRUE(opKeystore.HasPendingOpKeypair() == true);
    EXPECT_TRUE(opKeystore.HasOpKeypairForFabric(kFabricIndex) == false);

    // Before successful activation, cannot sign
    uint8_t message[] = { 1, 2, 3, 4 };
    P256ECDSASignature sig1;
    err = opKeystore.SignWithOpKeypair(kFabricIndex, ByteSpan{ message }, sig1);
    EXPECT_TRUE(err == CHIP_ERROR_INVALID_FABRIC_INDEX);

    // Activating with matching fabricIndex and matching public key succeeds
    err = opKeystore.ActivateOpKeypairForFabric(kFabricIndex, csrPublicKey2);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Activating does not store, and keeps pending
    EXPECT_TRUE(opKeystore.HasPendingOpKeypair() == true);
    EXPECT_TRUE(opKeystore.HasOpKeypairForFabric(kFabricIndex) == true);
    EXPECT_TRUE(opKeystore.HasOpKeypairForFabric(kBadFabricIndex) == false);

    // Can't sign for wrong fabric after activation
    P256ECDSASignature sig2;
    err = opKeystore.SignWithOpKeypair(kBadFabricIndex, ByteSpan{ message }, sig2);
    EXPECT_TRUE(err == CHIP_ERROR_INVALID_FABRIC_INDEX);

    // Can sign after activation
    err = opKeystore.SignWithOpKeypair(kFabricIndex, ByteSpan{ message }, sig2);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Signature matches pending key
    err = csrPublicKey2.ECDSA_validate_msg_signature(message, sizeof(message), sig2);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Signature does not match a previous pending key
    err = csrPublicKey1.ECDSA_validate_msg_signature(message, sizeof(message), sig2);
    EXPECT_TRUE(err == CHIP_ERROR_INVALID_SIGNATURE);

    // Committing with mismatching fabric fails, leaves pending
    err = opKeystore.CommitOpKeypairForFabric(kBadFabricIndex);
    EXPECT_TRUE(err == CHIP_ERROR_INVALID_FABRIC_INDEX);
    EXPECT_TRUE(opKeystore.HasPendingOpKeypair() == true);
    EXPECT_TRUE(opKeystore.HasOpKeypairForFabric(kFabricIndex) == true);

    // Committing key resets pending state
    err = opKeystore.CommitOpKeypairForFabric(kFabricIndex);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(opKeystore.HasPendingOpKeypair() == false);
    EXPECT_TRUE(opKeystore.HasOpKeypairForFabric(kFabricIndex) == true);

    // After committing, signing works with the key that was pending
    P256ECDSASignature sig3;
    uint8_t message2[] = { 10, 11, 12, 13 };
    err                = opKeystore.SignWithOpKeypair(kFabricIndex, ByteSpan{ message2 }, sig3);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    err = csrPublicKey2.ECDSA_validate_msg_signature(message2, sizeof(message2), sig3);
    EXPECT_TRUE(err == CHIP_NO_ERROR);

    // Let's remove the opkey for a fabric, it disappears
    err = opKeystore.RemoveOpKeypairForFabric(kFabricIndex);
    EXPECT_TRUE(err == CHIP_NO_ERROR);
    EXPECT_TRUE(opKeystore.HasPendingOpKeypair() == false);
    EXPECT_TRUE(opKeystore.HasOpKeypairForFabric(kFabricIndex) == false);
}

TEST_F(TestPSAOpKeyStore, TestEphemeralKeys)
{
    PSAOperationalKeystore opKeyStore;

    Crypto::P256ECDSASignature sig;
    uint8_t message[] = { 'm', 's', 'g' };

    Crypto::P256Keypair * ephemeralKeypair = opKeyStore.AllocateEphemeralKeypairForCASE();
    EXPECT_TRUE(ephemeralKeypair != nullptr);
    NL_TEST_ASSERT_SUCCESS(inSuite, ephemeralKeypair->Initialize(Crypto::ECPKeyTarget::ECDSA));

    NL_TEST_ASSERT_SUCCESS(inSuite, ephemeralKeypair->ECDSA_sign_msg(message, sizeof(message), sig));
    NL_TEST_ASSERT_SUCCESS(inSuite, ephemeralKeypair->Pubkey().ECDSA_validate_msg_signature(message, sizeof(message), sig));

    opKeyStore.ReleaseEphemeralKeypair(ephemeralKeypair);
}
