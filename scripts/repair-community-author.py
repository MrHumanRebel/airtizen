#!/usr/bin/env python3
"""Reissue the deliberately public sideload identity under the official SDK CA.
Preserves AirTizen's RSA key; never use this helper for private signing identities.
"""
import argparse
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.serialization import pkcs12
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.x509.oid import ExtendedKeyUsageOID

ROOT = Path(__file__).resolve().parents[1]
CA_SHA256 = '2c81253e50c19d8d3b8f365de5208b00bfbda34cc2fac71ef021190516eb5944'


def repair(sdk, output):
    source = ROOT / 'certificates/community-author.p12'
    key, original, _ = pkcs12.load_key_and_certificates(source.read_bytes(), b'1234')
    directory = Path(sdk) / 'tools/certificate-generator/certificates/developer'
    data = (directory / 'tizen-developer-ca.cer').read_bytes()
    ca = x509.load_pem_x509_certificate(data[data.index(b'-----BEGIN CERTIFICATE-----'):])
    if ca.fingerprint(hashes.SHA256()).hex() != CA_SHA256:
        raise ValueError('Unexpected official SDK developer CA')
    # This password and CA key are intentionally distributed in the official SDK.
    values = dict(line.strip().split('=', 1) for line in (directory / 'conf.ini').read_text().splitlines() if '=' in line)
    ca_key = serialization.load_pem_private_key((directory / 'tizen-developer-ca-privatekey.pem').read_bytes(), values['PASSWD_OF_ISSUER_FOR_DEV'].encode())
    if ca_key.public_key().public_numbers() != ca.public_key().public_numbers():
        raise ValueError('SDK CA key does not match its certificate')
    cert = (x509.CertificateBuilder().subject_name(original.subject).issuer_name(ca.subject)
            .public_key(key.public_key()).serial_number(x509.random_serial_number())
            .not_valid_before(max(original.not_valid_before_utc, ca.not_valid_before_utc))
            .not_valid_after(ca.not_valid_after_utc)
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), critical=True)
            .add_extension(x509.KeyUsage(True, False, False, False, False, False, False, False, False), critical=False)
            .add_extension(x509.ExtendedKeyUsage([ExtendedKeyUsageOID.CODE_SIGNING]), critical=False)
            .add_extension(x509.SubjectKeyIdentifier.from_public_key(key.public_key()), critical=False)
            .add_extension(x509.AuthorityKeyIdentifier.from_issuer_public_key(ca.public_key()), critical=False)
            .sign(ca_key, hashes.SHA256()))
    ca.public_key().verify(cert.signature, cert.tbs_certificate_bytes, padding.PKCS1v15(), cert.signature_hash_algorithm)
    data = pkcs12.serialize_key_and_certificates(b'AirTizen', key, cert, [ca],
            serialization.PrivateFormat.PKCS12.encryption_builder().kdf_rounds(50000)
            .key_cert_algorithm(pkcs12.PBES.PBESv1SHA1And3KeyTripleDESCBC).hmac_hash(hashes.SHA1()).build(b'1234'))
    Path(output).write_bytes(data)
    print('Preserved author public key; issued SDK-chain certificate:', cert.fingerprint(hashes.SHA256()).hex())


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdk', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    repair(args.sdk, args.output)
