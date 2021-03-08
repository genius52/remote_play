// Copyright 2017 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/crx_file/crx_creator.h"

#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/stl_util.h"
#include "components/crx_file/crx3.pb.h"
#include "components/crx_file/crx_file.h"
#include "crypto/rsa_private_key.h"
#include "crypto/sha2.h"
#include "crypto/signature_creator.h"

namespace crx_file {

namespace {

std::string GetCrxId(const std::string& key) {
  uint8_t hash[16] = {};  // CRX IDs are 16 bytes long.
  crypto::SHA256HashString(key, hash, sizeof(hash));
  static_assert(sizeof(char) == sizeof(uint8_t), "Unsupported char size.");
  return std::string(reinterpret_cast<char*>(hash), sizeof(hash));
}

// Read to the end of the file, updating the signer.
CreatorResult ReadAndSignArchive(base::File* file,
                                 crypto::SignatureCreator* signer,
                                 std::vector<uint8_t>* signature) {
  uint8_t buffer[1 << 12] = {};
  int read = 0;
  static_assert(sizeof(char) == sizeof(uint8_t), "Unsupported char size.");
  while ((read = file->ReadAtCurrentPos(reinterpret_cast<char*>(buffer),
                                        base::size(buffer))) > 0) {
    if (!signer->Update(buffer, read))
      return CreatorResult::ERROR_SIGNING_FAILURE;
  }
  if (read < 0)
    return CreatorResult::ERROR_SIGNING_FAILURE;
  return signer->Final(signature) ? CreatorResult::OK
                                  : CreatorResult::ERROR_SIGNING_FAILURE;
}

bool WriteBuffer(base::File* file, const char buffer[], int len) {
  return file->WriteAtCurrentPos(buffer, len) == len;
}

bool WriteArchive(base::File* out, base::File* in) {
  char buffer[1 << 12] = {};
  int read = 0;
  in->Seek(base::File::Whence::FROM_BEGIN, 0);
  while ((read = in->ReadAtCurrentPos(buffer, base::size(buffer))) > 0) {
    if (out->WriteAtCurrentPos(buffer, read) != read)
      return false;
  }
  return read == 0;
}

}  // namespace

CreatorResult Create(const base::FilePath& output_path,
                     const base::FilePath& zip_path,
                     crypto::RSAPrivateKey* signing_key) {
  // Get the public key.
  std::vector<uint8_t> public_key;
  signing_key->ExportPublicKey(&public_key);
  const std::string public_key_str(public_key.begin(), public_key.end());

  // Assemble SignedData section.
  SignedData signed_header_data;
  signed_header_data.set_crx_id(GetCrxId(public_key_str));
  const std::string signed_header_data_str =
      signed_header_data.SerializeAsString();
  const int signed_header_size = signed_header_data_str.size();
  const uint8_t signed_header_size_octets[] = {
      signed_header_size, signed_header_size >> 8, signed_header_size >> 16,
      signed_header_size >> 24};

  // Create a signer, init with purpose, SignedData length, run SignedData
  // through, run ZIP through.
  auto signer = crypto::SignatureCreator::Create(
      signing_key, crypto::SignatureCreator::HashAlgorithm::SHA256);
  signer->Update(kSignatureContext, base::size(kSignatureContext));
  signer->Update(signed_header_size_octets,
                 base::size(signed_header_size_octets));
  signer->Update(
      reinterpret_cast<const uint8_t*>(signed_header_data_str.data()),
      signed_header_data_str.size());
  base::File file(zip_path, base::File::FLAG_OPEN | base::File::FLAG_READ);
  if (!file.IsValid())
    return CreatorResult::ERROR_FILE_NOT_READABLE;
  std::vector<uint8_t> signature;
  const CreatorResult signing_result =
      ReadAndSignArchive(&file, signer.get(), &signature);
  if (signing_result != CreatorResult::OK)
    return signing_result;

  // Create CRXFileHeader.
  CrxFileHeader header;
  AsymmetricKeyProof* proof = header.add_sha256_with_rsa();
  proof->set_public_key(public_key_str);
  proof->set_signature(std::string(signature.begin(), signature.end()));
  header.set_signed_header_data(signed_header_data_str);
  const std::string header_str = header.SerializeAsString();
  const int header_size = header_str.size();
  const uint8_t header_size_octets[] = {header_size, header_size >> 8,
                                        header_size >> 16, header_size >> 24};

  // Write CRX.
  const uint8_t format_version_octets[] = {3, 0, 0, 0};
  base::File crx(output_path,
                 base::File::FLAG_CREATE_ALWAYS | base::File::FLAG_WRITE);
  if (!crx.IsValid())
    return CreatorResult::ERROR_FILE_NOT_WRITABLE;
  static_assert(sizeof(char) == sizeof(uint8_t), "Unsupported char size.");
  if (!WriteBuffer(&crx, kCrxFileHeaderMagic, kCrxFileHeaderMagicSize) ||
      !WriteBuffer(&crx, reinterpret_cast<const char*>(format_version_octets),
                   base::size(format_version_octets)) ||
      !WriteBuffer(&crx, reinterpret_cast<const char*>(header_size_octets),
                   base::size(header_size_octets)) ||
      !WriteBuffer(&crx, header_str.c_str(), header_str.length()) ||
      !WriteArchive(&crx, &file)) {
    return CreatorResult::ERROR_FILE_WRITE_FAILURE;
  }
  return CreatorResult::OK;
}

}  // namespace crx_file
