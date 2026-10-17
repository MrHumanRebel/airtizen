#!/usr/bin/env python3
"""Validate the default public author's SDK chain, purpose and stable key."""
import argparse
import base64
import xml.etree.ElementTree as ET
from zipfile import ZipFile
from datetime import datetime, timezone
import hashlib
from pathlib import Path
from cryptography import x509
from cryptography.hazmat.primitives import hashes, serialization
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives.serialization import pkcs12
from cryptography.x509.oid import ExtendedKeyUsageOID

KEY_SHA256 = '8ccd12c0ccec96b46ffb80f62b9309bc58e4f1f165f2ea6ebd38442e596d63c0'
CA_SHA256 = '2c81253e50c19d8d3b8f365de5208b00bfbda34cc2fac71ef021190516eb5944'


def check(path, package=None):
    key, leaf, chain = pkcs12.load_key_and_certificates(Path(path).read_bytes(), b'1234')
    if not key or not leaf or len(chain) != 1:
        raise ValueError('Public author must include its SDK developer CA chain')
    ca = chain[0]
    if ca.fingerprint(hashes.SHA256()).hex() != CA_SHA256 or leaf.issuer != ca.subject:
        raise ValueError('Public author is not issued by the expected Tizen Developers CA')
    ca.public_key().verify(leaf.signature, leaf.tbs_certificate_bytes, padding.PKCS1v15(), leaf.signature_hash_algorithm)
    now = datetime.now(timezone.utc)
    for certificate in (leaf, ca):
        if not certificate.not_valid_before_utc <= now < certificate.not_valid_after_utc:
            raise ValueError('Public author SDK chain expired/not yet valid; renew the official identity')
    if leaf.extensions.get_extension_for_class(x509.BasicConstraints).value.ca:
        raise ValueError('Author must be an end-entity certificate')
    if not leaf.extensions.get_extension_for_class(x509.KeyUsage).value.digital_signature:
        raise ValueError('Author lacks digital-signature usage')
    if ExtendedKeyUsageOID.CODE_SIGNING not in leaf.extensions.get_extension_for_class(x509.ExtendedKeyUsage).value:
        raise ValueError('Author lacks code-signing purpose')
    if key.public_key().public_numbers() != leaf.public_key().public_numbers() or key.key_size < 2048:
        raise ValueError('Author key mismatch or weak key')
    if hashlib.sha256(key.public_key().public_bytes(serialization.Encoding.DER, serialization.PublicFormat.SubjectPublicKeyInfo)).hexdigest() != KEY_SHA256:
        raise ValueError("Default community author key changed; preserve upgrade identity")
    if package is not None:
        with ZipFile(package) as archive:
            if archive.getinfo('author-signature.xml').file_size > 1024*1024:
                raise ValueError('Oversized author signature')
            xml = ET.fromstring(archive.read('author-signature.xml'))
            actual = [base64.b64decode(item.text) for item in xml.findall('.//{http://www.w3.org/2000/09/xmldsig#}X509Certificate')]
            expected = [certificate.public_bytes(serialization.Encoding.DER) for certificate in (leaf, ca)]
            if actual != expected:
                raise ValueError('WGT did not embed the default author and its SDK CA chain')
    return leaf


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('identity', type=Path)
    parser.add_argument("--package",type=Path)
    args=parser.parse_args()
    check(args.identity,args.package)
    print('Default public author SDK chain, dates and code-signing purpose verified')
