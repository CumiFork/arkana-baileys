import makeWASocket, {
  DisconnectReason,
  fetchLatestBaileysVersion,
  useMultiFileAuthState
} from 'arkana-baileys'

const DEMO_JID = '6281234567890@s.whatsapp.net'

async function connect() {
  const { state, saveCreds } = await useMultiFileAuthState('./auth')
  const { version } = await fetchLatestBaileysVersion()

  const sock = makeWASocket({ version, auth: state })

  sock.ev.on('creds.update', saveCreds)
  sock.ev.on('connection.update', ({ connection, lastDisconnect, qr }) => {
    if (qr) console.log('Scan QR ini:', qr)
    if (connection === 'close') {
      const loggedOut = lastDisconnect?.error?.output?.statusCode === DisconnectReason.loggedOut
      if (!loggedOut) process.exit(1)
    }
  })

  return sock
}

function inspectResolver(sock) {
  const pnToLid = sock.resolveJid(DEMO_JID)
  const lidToPn = sock.resolveJid('123456@lid')

  console.log('PN -> LID:', pnToLid)
  console.log('LID -> PN:', lidToPn)
  console.log('Map ukuran:', sock.lidToPn.size, sock.pnToLid.size)
}

async function sendMixedButtons(sock) {
  return sock.sendInteractive(DEMO_JID, {
    text: 'Pilih salah satu:',
    title: 'Arkana Showcase',
    footer: 'Contoh native flow',
    buttons: [
      { type: 'quick_reply', display_text: 'Lihat harga', id: 'harga' },
      { type: 'cta_url', display_text: 'Buka repo', url: 'https://github.com/CumiFork/arkana-baileys' },
      { type: 'cta_copy', display_text: 'Salin kode', copy_code: 'ARKANA50', id: 'copy' }
    ]
  })
}

async function sendFlowResponse(sock) {
  return sock.sendGalaxy(DEMO_JID, {
    text: 'Flow selesai',
    name: 'galaxy_message',
    paramsJson: { status: 'success', screen: 'result' },
    version: 1
  })
}

async function runShowcase(sock) {
  inspectResolver(sock)
  const interactive = await sendMixedButtons(sock)
  console.log('Tombol terkirim:', interactive?.key?.id)
  const ids = await sendFlowResponse(sock)
  console.log('Flow response terkirim:', ids)
}

const sock = await connect()

sock.ev.on('connection.update', ({ connection }) => {
  if (connection === 'open') {
    runShowcase(sock).catch(error => console.error('Showcase gagal:', error))
  }
})
