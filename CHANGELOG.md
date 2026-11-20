# Changelog

Catatan rilis untuk `arkana-baileys`. Format mengikuti [Keep a Changelog](https://keepachangelog.com/id/1.1.0/).

## [Unreleased]

### Ditambahkan

- `sock.sendCarousel`, `sock.sendAlbum`, `sock.sendPollV3`, `sock.sendEvent`, `sock.sendPaymentRequest`, `sock.sendOrder`, `sock.sendViewOnce`, dan `sock.sendAdReply`.
- `sock.getLid`, `sock.getPn`, `sock.signalJid`, dan `sock.flushLidMapping` untuk resolusi eksplisit dan penulisan cache resolver.
- Persistensi `lid-mapping.json` dengan debounced write melalui opsi `lidMappingDir`, `lidMappingPath`, atau `lidMappingDebounceMs`.
- Field `addonActionSection = 115` pada `Message`, lengkap dengan binding ESM dan deklarasi TypeScript.
- Deklarasi TypeScript untuk seluruh helper mod, properti `msg.senderPn`, dan `msg.senderLid`.

### Diperbaiki

- Injeksi `<biz>` kini menggabungkan `additionalNodes` yang sudah ada dan tidak menimpa node `<bot biz_bot="1"/>` atau node kustom lain.
- Nama `<native_flow>` diambil dinamis dari tombol pertama, dengan fallback `galaxy_message` dan `mpm`.
- Sanitizer suffix perangkat kini aman untuk `@g.us`, `@newsletter`, `@lid`, dan `@broadcast`.
- `relayMessage` memakai JID LID ketika pasangan tersedia agar sesi Signal yang dipakai tetap konsisten.
- `downloadMedia` menerima `buffer`, `stream`, `base64`, dan `data-url`, serta membuka wrapper view once dan ephemeral.

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
