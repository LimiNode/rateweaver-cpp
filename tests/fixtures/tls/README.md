The public key and certificate in this directory are test fixtures only. The
certificate trusts `localhost`; the private key is deliberately public and must
never be used outside a loopback test server. The local HTTPS test supplies this
certificate explicitly and also verifies rejection of a different hostname.

Generated with OpenSSL:

```sh
openssl req -x509 -newkey rsa:2048 -nodes -keyout localhost-key.pem \
  -out localhost.pem -days 36500 -subj '/CN=localhost' \
  -addext 'subjectAltName=DNS:localhost' \
  -addext 'basicConstraints=critical,CA:TRUE'
```
