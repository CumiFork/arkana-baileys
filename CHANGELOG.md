# Changelog

Catatan rilis untuk `arkana-baileys`. Format mengikuti [Keep a Changelog](https://keepachangelog.com/id/1.1.0/).

## [1.3.0-mod] - 2026-11-20

### Ditambahkan

- `sock.sendInteractive(jid, content, quoted)` sebagai wrapper `sendMessage` untuk tombol campuran, header media, dan teks.
- `sock.sendGalaxy(jid, options)` untuk mengirim `interactiveResponseMessage` lewat `relayMessage`, termasuk opsi `count`, `delay`, `name`, `paramsJson`, dan konteks balasan.
- `sock.resolveJid`, `sock.lidToPn`, dan `sock.pnToLid` untuk resolusi dua arah antara `@lid` dan `@s.whatsapp.net`.
- `examples/mod-showcase.js` sebagai contoh ESM untuk koneksi socket, resolver JID, tombol interaktif, dan flow response.
- `assets/banner.svg` dan penyusunan ulang README dengan tabel perbandingan fitur, panduan Termux, dan rincian struktur direktori.

### Diperbaiki

- Node `<biz>` kini disuntikkan otomatis untuk `interactiveMessage`, `interactiveResponseMessage`, `buttonsMessage`, `templateMessage`, dan `carouselMessage`, termasuk saat pesan dibungkus `viewOnceMessageV2` atau `ephemeralMessage`.
- Response native flow tidak lagi dikirim sebagai pesan teks biasa, sehingga menghindari `xml-not-well-formed (500)`.
- Suffix perangkat pada JID seperti `:2@s.whatsapp.net` atau `:2@c.us` dibersihkan sebelum `sendMessage`, `reply`, dan `relayMessage`.
- Pasangan `@lid` dan nomor telepon kini dipelajari dari `messaging-history.set`, metadata grup, dan `messages.upsert`, selain `contacts` dan `chats`.

### Diubah

- Resolver LID diperluas dengan `toPn`, `toLid`, dan `resolveJid`, lalu dipasang di lapisan socket agar tersedia langsung dari `sock`.
- Header interaktif mendukung `title`, `subtitle`, `footer`, `image`, `video`, dan `document` dalam satu alur pengiriman.

### Kompatibilitas

- ESM penuh, diverifikasi di Node.js v24.22.3.
- Mendukung Node.js v20 sebagai versi minimum.
- Pemasangan di Termux Android memakai `npm install --no-bin-links --no-audit --no-fund`.
- `WAProto/index.proto` tetap menjadi skema kanonik untuk `pollCreationMessageV3`, `eventMessage.extraGuestsAllowed`, album, dan field interaktif tambahan.
