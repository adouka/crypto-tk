
// libsse_crypto - An abstraction layer for high level cryptographic features.
// Copyright (C) 2015-2017 Raphael Bost
//
// This file is part of libsse_crypto.
//
// libsse_crypto is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// libsse_crypto is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with libsse_crypto.  If not, see <http://www.gnu.org/licenses/>.
//

#include "tdp_impl_mbedtls.hpp"

#include "mbedtls/bignum.h"
#include "mbedtls/rsa.h"
#include "mbedtls/rsa_io.h"
#include "prf.hpp"
#include "random.hpp"

#include <cstring>

#include <exception>
#include <iomanip>
#include <iostream>

#include <sodium/utils.h>


namespace sse {

namespace crypto {


#define RSA_MODULUS_SIZE (TdpInverse::kMessageSize * 8)

#define RSA_PK 0x10001L // RSA_F4 for OpenSSL


static void zeroize_rsa(mbedtls_rsa_context* rsa)
{
    mbedtls_mpi_lset(&rsa->N, 0);
    mbedtls_mpi_lset(&rsa->E, 0);

    mbedtls_mpi_lset(&rsa->D, 0);
    mbedtls_mpi_lset(&rsa->P, 0);
    mbedtls_mpi_lset(&rsa->Q, 0);
    mbedtls_mpi_lset(&rsa->DP, 0);
    mbedtls_mpi_lset(&rsa->DQ, 0);
    mbedtls_mpi_lset(&rsa->QP, 0);
    mbedtls_mpi_lset(&rsa->RN, 0);
    mbedtls_mpi_lset(&rsa->RQ, 0);
    mbedtls_mpi_lset(&rsa->Vi, 0);
    mbedtls_mpi_lset(&rsa->Vf, 0);
}

// mbedTLS implementation of the trapdoor permutation

TdpImpl_mbedTLS::TdpImpl_mbedTLS()
{
    mbedtls_rsa_init(&rsa_key_, 0, 0);
}
TdpImpl_mbedTLS::TdpImpl_mbedTLS(const std::string& pk)
{
    mbedtls_rsa_init(&rsa_key_, 0, 0);

    // parse the public key
    if (mbedtls_rsa_parse_public_key(
            &rsa_key_,
            reinterpret_cast<const unsigned char*>(pk.c_str()),
            pk.length() + 1)
        != 0) {
        throw std::runtime_error("Invalid RSA public key");
    }

    if (mbedtls_rsa_check_pubkey(&rsa_key_) != 0) {
        /* LCOV_EXCL_START */
        throw std::runtime_error("Invalid public key generated during the TDP "
                                 "initialization");
        /* LCOV_EXCL_STOP */
    }
}

TdpImpl_mbedTLS::TdpImpl_mbedTLS(const TdpImpl_mbedTLS& tdp)
{
    mbedtls_rsa_init(&rsa_key_, 0, 0); /* LCOV_EXCL_LINE */
    if (mbedtls_rsa_copy(&rsa_key_, &tdp.rsa_key_) != 0) {
        throw std::runtime_error(
            "Error when copying an RSA private key"); /* LCOV_EXCL_LINE */
    }

    if (mbedtls_rsa_check_pubkey(&rsa_key_) != 0) {
        /* LCOV_EXCL_START */
        throw std::runtime_error("Invalid public key generated by the TDP copy "
                                 "constructor");
        /* LCOV_EXCL_STOP */
    }
}

inline size_t TdpImpl_mbedTLS::rsa_size() const
{
    return rsa_key_.len;
}

TdpImpl_mbedTLS::~TdpImpl_mbedTLS()
{
    zeroize_rsa(&rsa_key_);
    mbedtls_rsa_free(&rsa_key_);
}

TdpImpl_mbedTLS& TdpImpl_mbedTLS::operator=(const TdpImpl_mbedTLS& t)
{
    if (this != &t) {
        mbedtls_rsa_copy(&rsa_key_, &(t.rsa_key_));
    }

    return *this;
}

std::string TdpImpl_mbedTLS::public_key() const
{
    int           ret;
    unsigned char buf[5000];

    ret = mbedtls_rsa_write_pubkey_pem(
        const_cast<mbedtls_rsa_context*>(&rsa_key_), buf, sizeof(buf));

    if (ret != 0) {
        throw std::runtime_error(
            "Error when serializing the RSA public key. Error code: "
            + std::to_string(ret)); /* LCOV_EXCL_LINE */
    }
    std::string v(reinterpret_cast<const char*>(buf));

    sodium_memzero(buf, sizeof(buf));

    return v;
}

void TdpImpl_mbedTLS::eval(const std::string& in, std::string& out) const
{
    if (in.size() != rsa_size()) {
        throw std::invalid_argument("Invalid TDP input size. Input size should "
                                    "be kMessageSpaceSize bytes long.");
    }

    std::array<uint8_t, kMessageSpaceSize> in_array;


    memcpy(in_array.data(), in.data(), kMessageSpaceSize);

    auto out_array = eval(in_array);

    out = std::string(out_array.begin(), out_array.end());


    sodium_memzero(in_array.data(), in_array.size());
    sodium_memzero(out_array.data(), out_array.size());
}

std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize> TdpImpl_mbedTLS::eval(
    const std::array<uint8_t, kMessageSpaceSize>& in) const
{
    std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize> out;


    if (in.size() != rsa_size()) {
        throw std::runtime_error(
            "Invalid TDP input size. Input size should be kMessageSpaceSize "
            "bytes long."); /* LCOV_EXCL_LINE */
    }

    int         ret;
    mbedtls_mpi x;
    mbedtls_mpi_init(&x);

    // deserialize the integer
    ret = mbedtls_mpi_read_binary(&x, in.data(), in.size());

    if (ret != 0) {
        throw std::runtime_error(
            "Unable to read the TDP input"); /* LCOV_EXCL_LINE */
    }

    // in case we were given an input larger than the RSA modulus
    ret = mbedtls_mpi_mod_mpi(&x, &x, &rsa_key_.N);
    if (ret != 0) {
        throw std::runtime_error(
            "Error when reducing the RSA input mod N"); /* LCOV_EXCL_LINE */
    }

    // calling mbedtls_rsa_public is not ideal here as it would require us to
    // re-serialize the input

#if defined(MBEDTLS_THREADING_C)
    if (mbedtls_mutex_lock(&rsa_key_->mutex) != 0)
        throw std::runtime_error(
            "Unable to lock the RSA context"); /* LCOV_EXCL_LINE */
#endif

    ret = mbedtls_mpi_exp_mod(&x, &x, &rsa_key_.E, &rsa_key_.N, &rsa_key_.RN);


#if defined(MBEDTLS_THREADING_C)
    if (mbedtls_mutex_unlock(&rsa_key_->mutex) != 0)
        throw std::runtime_error(
            "Unable to unlock the RSA context"); /* LCOV_EXCL_LINE */
#endif

    if (ret != 0) {
        throw std::runtime_error(
            "Error during the modular exponentiation"); /* LCOV_EXCL_LINE */
    }

    mbedtls_mpi_write_binary(&x, out.data(), out.size());
    mbedtls_mpi_lset(&x, 0); // erase the temporary variable
    mbedtls_mpi_free(&x);

    return out;
}


std::string TdpImpl_mbedTLS::sample() const
{
    std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize> tmp
        = sample_array();

    std::string out(tmp.begin(), tmp.end());

    sodium_memzero(tmp.data(), tmp.size());

    return out;
}

std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize> TdpImpl_mbedTLS::
    sample_array() const
{
    mbedtls_mpi x;
    mbedtls_mpi_init(&x);
    std::array<uint8_t, kMessageSpaceSize> out;
    std::fill(out.begin(), out.end(), 0);

    int ret = 0;

    ret = mbedtls_mpi_fill_random(
        &x, Tdp::kRSAPrfSize, mbedTLS_rng_wrap, nullptr);

    if (ret != 0) {
        throw std::runtime_error(
            "Error during random TDP message generation"); /* LCOV_EXCL_LINE */
    }

    ret = mbedtls_mpi_mod_mpi(&x, &x, &rsa_key_.N);

    if (ret != 0) {
        throw std::runtime_error(
            "Error modulo computation"); /* LCOV_EXCL_LINE */
    }

    mbedtls_mpi_write_binary(&x, out.data(), out.size());
    mbedtls_mpi_lset(&x, 0);
    mbedtls_mpi_free(&x);

    return out;
}

std::string TdpImpl_mbedTLS::generate(const Prf<Tdp::kRSAPrfSize>& prg,
                                      const std::string&           seed) const
{
    std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize> tmp
        = generate_array(prg, seed);

    std::string out(tmp.begin(), tmp.end());

    sodium_memzero(tmp.data(), tmp.size());

    return out;
}

std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize> TdpImpl_mbedTLS::
    generate_array(const Prf<Tdp::kRSAPrfSize>& prg,
                   const std::string&           seed) const
{
    std::array<uint8_t, Tdp::kRSAPrfSize> rnd = prg.prf(seed);

    mbedtls_mpi x;
    mbedtls_mpi_init(&x);
    std::array<uint8_t, kMessageSpaceSize> out;
    std::fill(out.begin(), out.end(), 0);

    int ret = 0;

    ret = mbedtls_mpi_read_binary(&x, rnd.data(), rnd.size());

    if (ret != 0) {
        throw std::runtime_error(
            "Unable to read the randomness input"); /* LCOV_EXCL_LINE */
    }

    // take the randomness mod N
    // it is ok to do that as we took randomness large enough
    // so that the bias of the obtained x is negligible
    ret = mbedtls_mpi_mod_mpi(&x, &x, &rsa_key_.N);
    if (ret != 0) {
        throw std::runtime_error(
            "Error when reducing the RSA input mod N"); /* LCOV_EXCL_LINE */
    }

    mbedtls_mpi_write_binary(&x, out.data(), out.size());
    mbedtls_mpi_lset(&x, 0);
    mbedtls_mpi_free(&x);

    return out;
}

std::string TdpImpl_mbedTLS::generate(
    Key<Prf<Tdp::kRSAPrfSize>::kKeySize>&& key,
    const std::string&                     seed) const
{
    std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize> tmp
        = generate_array(std::move(key), seed);

    std::string out(tmp.begin(), tmp.end());

    sodium_memzero(tmp.data(), tmp.size());

    return out;
}

std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize> TdpImpl_mbedTLS::
    generate_array(Key<Prf<Tdp::kRSAPrfSize>::kKeySize>&& key,
                   const std::string&                     seed) const
{
    Prf<Tdp::kRSAPrfSize> prg(std::move(key));

    return generate_array(prg, seed);
}

TdpInverseImpl_mbedTLS::TdpInverseImpl_mbedTLS()
{
    int ret;

    // initialize the key
    mbedtls_rsa_init(&rsa_key_, 0, 0);
    mbedtls_mpi_init(&phi_);
    mbedtls_mpi_init(&p_1_);
    mbedtls_mpi_init(&q_1_);

    // generate a new random key
    ret = mbedtls_rsa_gen_key(
        &rsa_key_, mbedTLS_rng_wrap, nullptr, RSA_MODULUS_SIZE, RSA_PK);


    if (ret != 0) {
        throw std::runtime_error(
            "Unable to inialize a new TDP private key. Error code: "
            + std::to_string(ret)); /* LCOV_EXCL_LINE */
    }

    if (mbedtls_rsa_check_pubkey(&rsa_key_) != 0) {
        throw std::runtime_error(
            "Invalid private key generated during the Inverse TDP "
            "initialization"); /* LCOV_EXCL_LINE */
    }

    if (mbedtls_mpi_sub_int(&p_1_, &rsa_key_.P, 1) != 0) {
        throw std::runtime_error(
            "Failed MPI substraction"); /* LCOV_EXCL_LINE */
    }
    if (mbedtls_mpi_sub_int(&q_1_, &rsa_key_.Q, 1) != 0) {
        throw std::runtime_error(
            "Failed MPI substraction"); /* LCOV_EXCL_LINE */
    }
    if (mbedtls_mpi_mul_mpi(&phi_, &p_1_, &q_1_) != 0) {
        throw std::runtime_error(
            "Failed MPI multiplication"); /* LCOV_EXCL_LINE */
    }
}

TdpInverseImpl_mbedTLS::TdpInverseImpl_mbedTLS(const std::string& sk)
{
    int ret;

    // initialize the key
    mbedtls_rsa_init(&rsa_key_, 0, 0);
    mbedtls_mpi_init(&phi_);
    mbedtls_mpi_init(&p_1_);
    mbedtls_mpi_init(&q_1_);

    // do not forget the '\0' character
    ret = mbedtls_rsa_parse_key(
        &rsa_key_,
        reinterpret_cast<const unsigned char*>(sk.c_str()),
        sk.length() + 1,
        nullptr,
        0);

    if (ret != 0) {
        throw std::runtime_error(
            "Error when reading the RSA private key. Error code: "
            + std::to_string(ret)); /* LCOV_EXCL_LINE */
    }

    if (mbedtls_rsa_check_pubkey(&rsa_key_) != 0) {
        throw std::runtime_error(
            "Invalid private key generated during the Inverse TDP "
            "initialization from existing secret key"); /* LCOV_EXCL_LINE */
    }

    if (mbedtls_mpi_sub_int(&p_1_, &rsa_key_.P, 1) != 0) {
        throw std::runtime_error(
            "Failed MPI substraction"); /* LCOV_EXCL_LINE */
    }
    if (mbedtls_mpi_sub_int(&q_1_, &rsa_key_.Q, 1) != 0) {
        throw std::runtime_error(
            "Failed MPI substraction"); /* LCOV_EXCL_LINE */
    }
    if (mbedtls_mpi_mul_mpi(&phi_, &p_1_, &q_1_) != 0) {
        throw std::runtime_error(
            "Failed MPI multiplication"); /* LCOV_EXCL_LINE */
    }
}

TdpInverseImpl_mbedTLS::~TdpInverseImpl_mbedTLS()
{
    mbedtls_mpi_lset(&p_1_, 0);
    mbedtls_mpi_lset(&q_1_, 0);
    mbedtls_mpi_lset(&phi_, 0);

    mbedtls_mpi_free(&p_1_);
    mbedtls_mpi_free(&q_1_);
    mbedtls_mpi_free(&phi_);
}

std::string TdpInverseImpl_mbedTLS::private_key() const
{
    int           ret;
    unsigned char buf[5000];

    ret = mbedtls_rsa_write_key_pem(&rsa_key_, buf, sizeof(buf));

    if (ret != 0) {
        throw std::runtime_error(
            "Error when serializing the RSA private key. Error code: "
            + std::to_string(ret)); /* LCOV_EXCL_LINE */
    }
    std::string v(reinterpret_cast<const char*>(buf));

    sodium_memzero(buf, sizeof(buf));

    return v;
}


void TdpInverseImpl_mbedTLS::invert(const std::string& in,
                                    std::string&       out) const
{
    int           ret;
    unsigned char rsa_out[rsa_size()];

    if (in.size() != rsa_size()) {
        throw std::invalid_argument("Invalid TDP input size. Input size should "
                                    "be kMessageSpaceSize bytes long.");
    }

    ret = mbedtls_rsa_private(&rsa_key_,
                              mbedTLS_rng_wrap,
                              nullptr,
                              reinterpret_cast<const unsigned char*>(in.data()),
                              rsa_out);

    if (ret != 0) {
        throw std::invalid_argument(
            "Error during the RSA private key operation. Code: "
            + std::to_string(ret)); /* LCOV_EXCL_LINE */
    }

    out = std::string(reinterpret_cast<char*>(rsa_out), rsa_size());

    sodium_memzero(rsa_out, rsa_size());
}

std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize> TdpInverseImpl_mbedTLS::
    invert(const std::array<uint8_t, kMessageSpaceSize>& in) const
{
    std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize> out;

    int ret = mbedtls_rsa_private(
        &rsa_key_, mbedTLS_rng_wrap, nullptr, in.data(), out.data());

    if (ret != 0) {
        throw std::invalid_argument(
            "Error during the RSA private key operation. Code: "
            + std::to_string(ret)); /* LCOV_EXCL_LINE */
    }

    return out;
}

// returns X = A^E mod N, even when N is even
// CAUTION!!!!: be aware that a timing attack would reveal E,
// contrary to mbedtls_mpi_exp_mod
static int insecure_mod_exp(mbedtls_mpi*       X,
                            const mbedtls_mpi* A,
                            const uint64_t     E,
                            const mbedtls_mpi* N)
{
    int      ret = 0;
    uint64_t exp = E;

    mbedtls_mpi base;
    mbedtls_mpi_init(&base);

    MBEDTLS_MPI_CHK(mbedtls_mpi_lset(X, 1));


    MBEDTLS_MPI_CHK(mbedtls_mpi_copy(&base, A));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&base, &base, N));

    while (exp > 0) {
        if (exp % 2 == 1) {
            MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(X, X, &base));
            MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(X, X, N));
        }
        exp >>= 1;
        MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&base, &base, &base));
        MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&base, &base, N));
    }

// cppcheck-suppress unusedLabel
cleanup:
    mbedtls_mpi_free(&base);
    return ret;
}

std::array<uint8_t, TdpInverseImpl_mbedTLS::kMessageSpaceSize>
TdpInverseImpl_mbedTLS::invert_mult(
    const std::array<uint8_t, kMessageSpaceSize>& in,
    uint32_t                                      order) const
{
    // we have to reimplement everything by hand here
    // for the moment, this is not a very secure implementation:
    // it does not use blinding when available
#pragma message("Potentially insecure RSA implementation")
    if (order == 0) {
        return in;
    }

    std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize> out;


    if (in.size() != rsa_size()) {
        throw std::invalid_argument(
            "Invalid TDP input size. Input size should be kMessageSpaceSize "
            "bytes long."); /* LCOV_EXCL_LINE */
    }

    int         ret;
    mbedtls_mpi x, mpi_order, d_p, d_q;
    mbedtls_mpi y_p, y_q;
    mbedtls_mpi y;
    mbedtls_mpi_init(&x);
    mbedtls_mpi_init(&mpi_order);
    mbedtls_mpi_init(&d_p);
    mbedtls_mpi_init(&d_q);
    mbedtls_mpi_init(&y_p);
    mbedtls_mpi_init(&y_q);
    mbedtls_mpi_init(&y);

    // deserialize the integer
    ret = mbedtls_mpi_read_binary(&x, in.data(), in.size());

    // in case we were given an input larger than the RSA modulus
    //    ret = mbedtls_mpi_mod_mpi(&x,&x,&rsa_key_.N);
    //    if (ret != 0) {
    //        throw std::runtime_error("Error when reducing the RSA input mod
    //        N"); /* LCOV_EXCL_LINE */
    //    }

    if (ret != 0) {
        throw std::runtime_error(
            "Unable to read the TDP input"); /* LCOV_EXCL_LINE */
    }

    // load the order
    mbedtls_mpi_lset(&mpi_order, order);

#if defined(MBEDTLS_THREADING_C)
    if (mbedtls_mutex_lock(&rsa_key_->mutex) != 0)
        throw std::runtime_error(
            "Unable to lock the RSA context"); /* LCOV_EXCL_LINE */
#endif

    // there is an issue with the following code:
    // mbedTLS mpi library does not allow for a modular exponentiation
    // where the module is even. And we were actually relying on that
    // to compute the adjusted exponent
    //    mbedtls_mpi_exp_mod(&d_p, &rsa_key_.DP, &mpi_order, &p_1_, nullptr);
    //    mbedtls_mpi_exp_mod(&d_q, &rsa_key_.DQ, &mpi_order, &q_1_, nullptr);

    // instead, we implemented the insecure_mod_exp function
    // it is definitely less secure than mbedtls_mpi_exp_mod
    // but it works with even modulis
    MBEDTLS_MPI_CHK(insecure_mod_exp(&d_p, &rsa_key_.DP, order, &p_1_));
    MBEDTLS_MPI_CHK(insecure_mod_exp(&d_q, &rsa_key_.DQ, order, &q_1_));

    MBEDTLS_MPI_CHK(
        mbedtls_mpi_exp_mod(&y_p, &x, &d_p, &rsa_key_.P, &rsa_key_.RP));
    MBEDTLS_MPI_CHK(
        mbedtls_mpi_exp_mod(&y_q, &x, &d_q, &rsa_key_.Q, &rsa_key_.RQ));

    /*
     * Y = (YP - YQ) * (Q^-1 mod P) mod P
     */

    MBEDTLS_MPI_CHK(mbedtls_mpi_sub_mpi(&y, &y_p, &y_q));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&y_p, &y, &rsa_key_.QP));
    MBEDTLS_MPI_CHK(mbedtls_mpi_mod_mpi(&y, &y_p, &rsa_key_.P));

    /*
     * Y = YQ + Y * Q
     */
    MBEDTLS_MPI_CHK(mbedtls_mpi_mul_mpi(&y_p, &y, &rsa_key_.Q));
    MBEDTLS_MPI_CHK(mbedtls_mpi_add_mpi(&y, &y_q, &y_p));

    ////    BN_mod_sub(h, y_p, y_q, get_rsa_key()->p, ctx);
    //    mbedtls_mpi_sub_mpi(&h, &y_p, &y_q);
    //    mbedtls_mpi_mod_mpi(&h, &h, &rsa_key_.P);
    //
    ////    BN_mod_mul(h, h, get_rsa_key()->iqmp, get_rsa_key()->p, ctx);
    //    mbedtls_mpi_mul_mpi(&h, &h, &rsa_key_.QP);
    //    mbedtls_mpi_mod_mpi(&h, &h, &rsa_key_.P);
    //
    //
    //    mbedtls_mpi_mul_mpi(&y, &h, &rsa_key_.Q);
    //    mbedtls_mpi_add_mpi(&y, &y, &y_q);


    // calling mbedtls_rsa_public is not ideal here as it would require us to
    // re-serialize the input

#if defined(MBEDTLS_THREADING_C)
    if (mbedtls_mutex_unlock(&rsa_key_->mutex) != 0)
        throw std::runtime_error(
            "Unable to unlock the RSA context"); /* LCOV_EXCL_LINE */
#endif

    if (mbedtls_mpi_write_binary(&y, out.data(), out.size()) != 0) {
        throw std::runtime_error("Error while writing RSA result to the out "
                                 "buffer"); /* LCOV_EXCL_LINE
                                             */
    }

    // cppcheck does not see the use of goto cleanup in the MBEDTLS_MPI_CHK
    // macros
// cppcheck-suppress unusedLabel
cleanup:
    // erase the temporary variables
    mbedtls_mpi_lset(&x, 0);
    mbedtls_mpi_lset(&d_p, 0);
    mbedtls_mpi_lset(&d_q, 0);
    mbedtls_mpi_lset(&y_p, 0);
    mbedtls_mpi_lset(&y_q, 0);
    mbedtls_mpi_lset(&mpi_order, 0);

    mbedtls_mpi_free(&x);
    mbedtls_mpi_free(&d_p);
    mbedtls_mpi_free(&d_q);
    mbedtls_mpi_free(&y_p);
    mbedtls_mpi_free(&y_q);
    mbedtls_mpi_free(&mpi_order);

    if (ret != 0) {
        throw std::runtime_error(
            "Error during the modular exponentiation"); /* LCOV_EXCL_LINE */
    }

    mbedtls_mpi_lset(&y, 0); // erase the temporary variable
    mbedtls_mpi_free(&y);

    return out;
}

void TdpInverseImpl_mbedTLS::invert_mult(const std::string& in,
                                         std::string&       out,
                                         uint32_t           order) const
{
    std::array<uint8_t, kMessageSpaceSize> in_array;

    memcpy(in_array.data(), in.data(), kMessageSpaceSize);

    auto out_array = invert_mult(in_array, order);

    out = std::string(out_array.begin(), out_array.end());

    sodium_memzero(out_array.data(), out_array.size());
}


TdpMultPoolImpl_mbedTLS::TdpMultPoolImpl_mbedTLS(const std::string& sk,
                                                 const uint8_t      size)
    : TdpImpl_mbedTLS(sk), keys_count_(size - 1)
{
    if (size == 0) {
        throw std::invalid_argument(
            "Invalid Multiple TDP pool input size. Pool size should be > 0.");
    }

    keys_ = new mbedtls_rsa_context[keys_count_];

    mbedtls_rsa_init(keys_, 0, 0);
    mbedtls_rsa_copy(keys_, &rsa_key_);

    mbedtls_mpi_mul_int(&keys_[0].E, &keys_[0].E, RSA_PK);

    if (mbedtls_rsa_check_pubkey(&keys_[0]) != 0) {
        throw std::runtime_error("Invalid public key generated during the TDP "
                                 "pool initialization"); /* LCOV_EXCL_LINE */
    }

    for (uint8_t i = 1; i < keys_count_; i++) {
        mbedtls_rsa_init(&keys_[i], 0, 0);
        mbedtls_rsa_copy(&keys_[i], &keys_[i - 1]);

        mbedtls_mpi_mul_int(&keys_[i].E, &keys_[i - 1].E, RSA_PK);

        if (mbedtls_rsa_check_pubkey(&keys_[i]) != 0) {
            throw std::runtime_error(
                "Invalid public key generated during the TDP pool "
                "initialization"); /* LCOV_EXCL_LINE */
        }
    }
}

TdpMultPoolImpl_mbedTLS::TdpMultPoolImpl_mbedTLS(
    const TdpMultPoolImpl_mbedTLS& pool_impl)
    : TdpImpl_mbedTLS(pool_impl), keys_count_(pool_impl.keys_count_)
{
    keys_ = new mbedtls_rsa_context[keys_count_];

    for (uint8_t i = 0; i < keys_count_; i++) {
        mbedtls_rsa_init(&keys_[i], 0, 0);

        mbedtls_rsa_copy(&keys_[i], &pool_impl.keys_[i]);


        if (mbedtls_rsa_check_pubkey(&keys_[i]) != 0) {
            throw std::runtime_error(
                "Invalid public key generated by the TDP pool copy "
                "constructor"); /* LCOV_EXCL_LINE */
        }
    }
}

TdpMultPoolImpl_mbedTLS& TdpMultPoolImpl_mbedTLS::operator=(
    const TdpMultPoolImpl_mbedTLS& t)
{
    if (this != &t) {
        for (uint8_t i = 0; i < keys_count_; i++) {
            zeroize_rsa(&keys_[i]);
            mbedtls_rsa_free(&keys_[i]);
        }

        if (keys_count_ != t.keys_count_) {
            // realloc the key array
            delete[] keys_;
            keys_count_ = t.keys_count_;
            keys_       = new mbedtls_rsa_context[keys_count_];
        }

        for (uint8_t i = 0; i < keys_count_; i++) {
            mbedtls_rsa_init(&keys_[i], 0, 0);

            mbedtls_rsa_copy(&keys_[i], &t.keys_[i]);

            if (mbedtls_rsa_check_pubkey(&keys_[i]) != 0) {
                throw std::runtime_error(
                    "Invalid public key generated by the TDP pool assignment "
                    "operator"); /* LCOV_EXCL_LINE */
            }
        }
    }
    return *this;
}

TdpMultPoolImpl_mbedTLS::~TdpMultPoolImpl_mbedTLS()
{
    for (uint8_t i = 0; i < keys_count_; i++) {
        zeroize_rsa(&keys_[i]);
        mbedtls_rsa_free(&keys_[i]);
    }
    delete[] keys_;
}

std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize>
TdpMultPoolImpl_mbedTLS::eval_pool(
    const std::array<uint8_t, kMessageSpaceSize>& in,
    const uint8_t                                 order) const
{
    std::array<uint8_t, TdpImpl_mbedTLS::kMessageSpaceSize> out;
    mbedtls_rsa_context*                                    key;
    if (order == 1) {
        // regular eval
        key = &rsa_key_;

    } else if (order <= maximum_order()) {
        // get the right RSA context, i.e. the one in keys_[order-1]
        key = &keys_[order - 2];
    } else {
        throw std::invalid_argument(
            "Invalid order for this TDP pool. The input order must be less "
            "than the maximum order supported by the pool, and strictly "
            "positive.");
    }


    if (in.size() != rsa_size()) {
        throw std::runtime_error(
            "Invalid TDP input size. Input size should be kMessageSpaceSize "
            "bytes long."); /* LCOV_EXCL_LINE */
    }

    int         ret;
    mbedtls_mpi x;
    mbedtls_mpi_init(&x);

    // deserialize the integer
    ret = mbedtls_mpi_read_binary(&x, in.data(), in.size());

    if (ret != 0) {
        throw std::runtime_error(
            "Unable to read the TDP input"); /* LCOV_EXCL_LINE */
    }

    // in case we were given an input larger than the RSA modulus
    //    ret = mbedtls_mpi_mod_mpi(&x,&x,&key->N);
    //    if (ret != 0) {
    //        throw std::runtime_error("Error when reducing the RSA input mod
    //        N"); /* LCOV_EXCL_LINE */
    //    }

    // calling mbedtls_rsa_public is not ideal here as it would require us to
    // re-serialize the input

#if defined(MBEDTLS_THREADING_C)
    if (mbedtls_mutex_lock(key->mutex) != 0)
        throw std::runtime_error(
            "Unable to lock the RSA context"); /* LCOV_EXCL_LINE */
#endif

    ret = mbedtls_mpi_exp_mod(&x, &x, &key->E, &key->N, &key->RN);


#if defined(MBEDTLS_THREADING_C)
    if (mbedtls_mutex_unlock(key->mutex) != 0)
        throw std::runtime_error(
            "Unable to unlock the RSA context"); /* LCOV_EXCL_LINE */
#endif

    if (ret != 0) {
        throw std::runtime_error(
            "Error during the modular exponentiation"); /* LCOV_EXCL_LINE */
    }

    mbedtls_mpi_write_binary(&x, out.data(), out.size());

    mbedtls_mpi_lset(&x, 0);
    mbedtls_mpi_free(&x);

    return out;
}


void TdpMultPoolImpl_mbedTLS::eval_pool(const std::string& in,
                                        std::string&       out,
                                        const uint8_t      order) const
{
    if (in.size() != rsa_size()) {
        throw std::invalid_argument("Invalid TDP input size. Input size should "
                                    "be kMessageSpaceSize bytes long.");
    }

    std::array<uint8_t, kMessageSpaceSize> a_in, a_out;
    memcpy(a_in.data(), in.data(), kMessageSpaceSize);

    a_out = eval_pool(a_in, order);

    out = std::string(a_out.begin(), a_out.end());

    sodium_memzero(a_out.data(), a_out.size());
}

uint8_t TdpMultPoolImpl_mbedTLS::maximum_order() const
{
    return keys_count_ + 1;
}

} // namespace crypto
} // namespace sse
