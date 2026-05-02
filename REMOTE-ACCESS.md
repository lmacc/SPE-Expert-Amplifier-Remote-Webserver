# Remote access from outside the LAN

Three patterns, pick the one that fits your situation. The first is
the recommended starting point — it's free, needs no router changes,
and never exposes your home IP.

| Pattern | Best for | Public surface | Cost |
|---|---|---|---|
| **Cloudflare Tunnel + Access** | Operating from anywhere with a real URL like `spe-remote.example.com` | Cloudflare edge only | Free for personal use |
| **Tailscale** | Just you + a few trusted devices, no public URL needed | None — peer-to-peer | Free up to 100 devices |
| **Self-hosted HTTPS + DDNS** | DIY purists who want everything on their own hardware | Your home IP, port 443 | Domain cost only |

The whole rest of this document is the Cloudflare Tunnel walkthrough.
Tailscale and the DIY path are summarised at the end.

---

## Cloudflare Tunnel + Access

### What you get

* A public HTTPS URL like `https://spe-remote.example.com/`.
* A real Let's Encrypt cert managed by Cloudflare, auto-renewing.
* Cloudflare Access in front of it: requires a one-time PIN to your
  email (or Google / GitHub / GitHub SSO) before the request reaches
  your Pi.
* No port forwarding on your router. The Pi makes an outbound tunnel
  to Cloudflare; nothing inbound is exposed.
* Works through CGNAT, restrictive ISPs, or hotel Wi-Fi for the
  *server* side.
* Your home IP is never revealed.

### What you need

1. **A domain** managed on Cloudflare. Any `.com` works
   (~$10–15/year through Cloudflare's registrar). If you already have
   one elsewhere, you can move just the DNS to Cloudflare for free.
2. **A free Cloudflare account.** [dash.cloudflare.com](https://dash.cloudflare.com/sign-up)
3. **The SPE Remote daemon already running** on the Pi at
   `http://localhost:8080/` — no auth, no TLS. Cloudflare handles both.

### Step 1 — Install cloudflared on the Pi

```bash
# 64-bit Pi OS (recommended):
sudo curl -L -o /usr/local/bin/cloudflared \
    https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-arm64

# 32-bit Pi OS:
# sudo curl -L -o /usr/local/bin/cloudflared \
#     https://github.com/cloudflare/cloudflared/releases/latest/download/cloudflared-linux-arm

sudo chmod +x /usr/local/bin/cloudflared
cloudflared --version
```

### Step 2 — Authorize this Pi against your Cloudflare account

```bash
cloudflared tunnel login
```

This prints a URL. Open it on any device, log in to Cloudflare, pick
the domain you want to use. cloudflared writes a certificate to
`~/.cloudflared/cert.pem`.

### Step 3 — Create the tunnel and point DNS at it

```bash
cloudflared tunnel create spe-remote
# This writes ~/.cloudflared/<UUID>.json — that's the tunnel's
# credentials. Note the UUID; we'll need it in the next step.

cloudflared tunnel route dns spe-remote spe-remote.example.com
# Replace example.com with your actual domain.
```

After this, `spe-remote.example.com` resolves to a Cloudflare edge
address that knows how to reach your tunnel.

### Step 4 — Configure the tunnel

Create `~/.cloudflared/config.yml`:

```yaml
tunnel: spe-remote
credentials-file: /home/pi/.cloudflared/<UUID>.json
ingress:
  - hostname: spe-remote.example.com
    service: http://localhost:8080
  # Catch-all — required, returns 404 for anything else.
  - service: http_status:404
```

Replace `<UUID>` with the value from Step 3. If the user running
cloudflared isn't `pi`, fix the path accordingly.

### Step 5 — Test it

```bash
cloudflared tunnel run spe-remote
```

You should see lines like
`Connection registered ... location=DUB`. Open
`https://spe-remote.example.com/` in any browser, anywhere — you'll
see your chassis. Stop with Ctrl-C; we'll set it up as a service
next.

### Step 6 — Run cloudflared as a service

```bash
sudo cp ~/.cloudflared/config.yml /etc/cloudflared/config.yml
sudo cp ~/.cloudflared/<UUID>.json /etc/cloudflared/
sudo cloudflared service install
sudo systemctl enable --now cloudflared
sudo systemctl status cloudflared
```

(There's an example unit file at
[`packaging/cloudflared.service`](packaging/cloudflared.service) if
you'd rather drop one in by hand.)

The tunnel now starts on boot and reconnects automatically if the
network drops.

### Step 7 — Lock it down with Cloudflare Access

Right now anyone who knows the URL can reach your amp. That's bad.
In the Cloudflare dashboard:

1. **Zero Trust** → **Access** → **Applications** → **Add an
   application**.
2. Type: **Self-hosted**.
3. Name: `SPE Remote`. Domain: `spe-remote.example.com`. Path: `/`.
4. **Identity providers**: tick **One-time PIN** (free, sends a code
   to your email — no Google/GitHub account needed). Optionally
   add Google / GitHub / Microsoft if you want SSO.
5. **Application policy**: Action **Allow**, Include
   **Emails** → enter your email address. Add a second policy for any
   co-operators if applicable.
6. Save.

Now `https://spe-remote.example.com/` shows a Cloudflare login page
first. Enter your email, get a 6-digit code, paste it in. After login
the cookie is good for 24 h by default (configurable up to 1 month).

### Verify

From any device, anywhere:

```
https://spe-remote.example.com/
```

* Cloudflare login screen → enter email → get PIN → enter PIN.
* Chassis loads. WebSocket connects. Status frames flow.
* Your home IP appears nowhere — `whois spe-remote.example.com`
  returns Cloudflare addresses.

`/api/health` is also gated by Access by default. To exempt the
health endpoint for uptime monitors, add a second Access application
on path `/api/health` with action **Bypass**.

### Cost

* Cloudflare Tunnel: **free**, unlimited bandwidth.
* Cloudflare Access: **free** for up to 50 users.
* Domain: ~$10/yr (one-off cost; Cloudflare Registrar is at-cost).

### Auth options — pick the depth you want

Five sensible combinations, in increasing order of paranoia. Pick
one. The first is what the walkthrough above sets up.

#### A — Cloudflare Access only (recommended default)

| Layer | State |
|---|---|
| Cloudflare Access (edge) | **On** — SSO / email PIN |
| Daemon Basic auth | Off |
| Daemon TLS | Off (Cloudflare terminates TLS at the edge) |
| `trust_lan` | True (cloudflared appears as `127.0.0.1` to the daemon) |

This is what the 7-step walkthrough above produces. Cloudflare does
the identity check; the daemon stays in its default no-auth config.
The tunnel arrives at the daemon as `127.0.0.1`, which `trust_lan`
treats as same-network, so no double-login.

Right answer for almost everyone. Skip ahead to "WebSockets through
Cloudflare" unless you have a specific reason to layer more.

#### B — Cloudflare Access + daemon Basic auth (belt and braces)

| Layer | State |
|---|---|
| Cloudflare Access | **On** |
| Daemon Basic auth | **On** |
| Daemon TLS | Off |
| `trust_lan` | **False** (so `127.0.0.1` from cloudflared still gets the auth challenge) |

Two independent identity gates. Useful if you don't fully trust
Cloudflare-the-company (or your CF account's 2FA), or if you want
the daemon to refuse traffic from any path that bypasses Access
(e.g. someone with shell access on the Pi running `curl localhost:8080`).

```bash
# 1. Hash a password.
spe-remoted --hash-password "your-password-here"

# 2. Edit /var/lib/spe-remote/spe-remote/config.json:
{
  "auth_user":           "operator",
  "auth_password_hash":  "pbkdf2-sha256$120000$...",
  "trust_lan":           false
}

# 3. systemctl restart spe-remoted
```

To get into the web UI: visit
`https://operator:your-password-here@spe-remote.example.com/` the
first time, so the browser carries credentials onto the WebSocket
upgrade. Cloudflare passes `Authorization: Basic` through to the
tunnel unchanged, the daemon validates it.

#### C — Cloudflare Access + daemon TLS (origin pinning)

| Layer | State |
|---|---|
| Cloudflare Access | **On** |
| Daemon Basic auth | Optional |
| Daemon TLS | **On**, with a Cloudflare Origin Certificate |
| `trust_lan` | True (or false if combining with Basic auth) |

Encrypts the hop *between* the Cloudflare edge and your Pi. The Pi
runs HTTPS using a free Cloudflare-issued origin cert that's only
trusted by Cloudflare's edge (not the public internet). Closes the
gap where someone on the same LAN as the Pi could sniff the tunnel
endpoint's plaintext.

```bash
# In the Cloudflare dashboard for your domain:
# SSL/TLS → Origin Server → Create Certificate → 15 year RSA.
# Save the cert as /etc/ssl/spe-origin.pem and key as
# /etc/ssl/spe-origin-key.pem (chmod 600 the key).

# Edit /etc/cloudflared/config.yml:
ingress:
  - hostname: spe-remote.example.com
    service: https://localhost:8080
    originRequest:
      caPool: /etc/cloudflared/cloudflare-origin-ca.pem
  - service: http_status:404

# Edit /var/lib/spe-remote/spe-remote/config.json:
{
  "cert_file": "/etc/ssl/spe-origin.pem",
  "key_file":  "/etc/ssl/spe-origin-key.pem"
}
```

Niche; Cloudflare Tunnels are already TLS-encrypted between the Pi
and the edge by default. Worth it only if you have a hard
requirement that nothing inside the tunnel connection touches
plaintext.

#### D — Daemon Basic auth + TLS, no Cloudflare (self-hosted)

| Layer | State |
|---|---|
| Cloudflare Access | n/a |
| Daemon Basic auth | **On** |
| Daemon TLS | **On**, public Let's Encrypt cert |
| `trust_lan` | **False** |

The "Self-hosted HTTPS + DDNS" pattern at the bottom of this
document. Maximum DIY, every layer on your hardware, every renewal
on your cron.

#### E — Tailscale + daemon Basic auth (private + paranoid)

| Layer | State |
|---|---|
| Tailscale | **On**, ACLs restricting which devices can reach the Pi |
| Daemon Basic auth | **On** (optional but recommended) |
| Daemon TLS | Optional (Tailscale already encrypts the tailnet) |
| `trust_lan` | **False** if combining with Basic auth |

Use Tailscale ACLs to restrict access to specific tagged devices
even within your tailnet. Daemon Basic auth catches the case where
one of those devices is later compromised. No public surface,
multiple revocation paths.

---

### WebSockets through Cloudflare

WebSockets work over Cloudflare Tunnel out of the box — no extra
config required. Both `ws://` *and* the daemon's plain WebSocket on
the same hostname/port will be upgraded transparently and arrive at
your tunnel as native WS connections.

One caveat that's true everywhere, not specific to Cloudflare: when
daemon Basic auth is enabled (option B or D), browsers don't carry
the `Authorization` header onto WebSocket upgrades from JavaScript
unless you put credentials in the URL the user types. Use the
`https://user:pass@host/` form for the first connection.

### Cloudflare Access service tokens (for automation)

If you want to script API calls (e.g. uptime monitor, home-assistant
integration), don't rely on the browser SSO flow. In the Cloudflare
dashboard:

1. **Zero Trust** → **Access** → **Service Auth** → **Create
   service token**.
2. Save the `CF-Access-Client-Id` and `CF-Access-Client-Secret`.
3. Add a second policy on your Access application: Action **Service
   Auth**, Include **Service Auth** → tick the token name.

Then from a script:

```bash
curl https://spe-remote.example.com/api/health \
    -H "CF-Access-Client-Id: <id>" \
    -H "CF-Access-Client-Secret: <secret>"
```

The browser SSO flow is unaffected — humans still get the email PIN.

### IP allowlisting (optional)

Even with SSO, you can pin access to specific source networks:

* **Zero Trust** → **Access** → **Applications** → your app →
  **Edit policies**.
* Add a policy: Action **Allow**, Include **Emails** *AND* **IP
  ranges** (`78.143.0.0/16` for your home / mobile carrier / VPN
  exit, etc.).

Or for a stricter setup, change the existing policy to **Require IP
ranges**, so SSO alone isn't enough — the request must also come
from an approved network.

### Health check exemption (for monitors)

By default `/api/health` sits behind Access too. Uptime monitors
can't follow an SSO flow, so:

* Add a *second* Access application on path `/api/health`.
* Set its policy: Action **Bypass**, Include **Everyone**.

`/api/health` is intentionally unauthenticated on the daemon side
already, so this doesn't expose anything sensitive — just liveness.

### Troubleshooting

* **`Tunnel returned 502 Bad Gateway`** — cloudflared can't reach
  `localhost:8080`. Check `systemctl status spe-remoted` on the Pi.
* **`This site can't provide a secure connection`** in browser —
  DNS hasn't propagated yet. Wait 1–2 minutes, retry.
* **Cloudflared keeps reconnecting** in `journalctl -u cloudflared
  -f` — usually the home internet is flaky; the tunnel re-establishes
  on its own. Nothing to fix.
* **The PIN email never arrives** — check spam, then in the Access
  dashboard verify the identity provider is enabled and your
  email is whitelisted in the application policy.

---

## Tailscale (alternative)

Use this if you don't want a public URL at all — just stable private
addresses you reach from your own devices.

```bash
curl -fsSL https://tailscale.com/install.sh | sh
sudo tailscale up
```

Open the URL it prints, log in with Google / GitHub / Microsoft. The
Pi gets a stable `100.x.y.z` address inside your tailnet and a name
like `spe-remote.tail-net.ts.net`.

On every device that should reach the Pi:

* **iOS / Android** — install the Tailscale app, log in with the same
  account.
* **macOS / Windows / Linux** — same install command, same login.

Then any of those devices reaches `http://spe-remote.tail-net.ts.net:8080/`
directly. No public surface, no DNS to manage, no certs.

If you want HTTPS over the tailnet too,
[`tailscale serve`](https://tailscale.com/kb/1242/tailscale-serve)
will issue a cert and proxy to `localhost:8080`. Same one-command
experience as Cloudflare Tunnel but private instead of public.

The daemon stays in its default config in either case.

---

## Self-hosted HTTPS + DDNS (DIY)

Only worth it if you have a specific reason to keep everything on
your hardware end-to-end. Outline:

1. **DDNS hostname.** Sign up for [DuckDNS](https://duckdns.org) or
   no-ip; get something like `mycall.duckdns.org` pointing at your
   home public IP. Run their update script on the Pi via cron.
2. **Port forward** TCP 443 → Pi:443 on your home router. Disable
   UPnP if you want only this one rule.
3. **Get a real Let's Encrypt cert** with certbot:
   ```bash
   sudo apt-get install certbot
   sudo certbot certonly --standalone -d mycall.duckdns.org
   ```
   Cert/key end up at `/etc/letsencrypt/live/mycall.duckdns.org/{fullchain,privkey}.pem`.
4. **Wire them into the daemon** (`/var/lib/spe-remote/spe-remote/config.json`):
   ```json
   {
     "cert_file":  "/etc/letsencrypt/live/mycall.duckdns.org/fullchain.pem",
     "key_file":   "/etc/letsencrypt/live/mycall.duckdns.org/privkey.pem",
     "http_port":  443,
     "auth_user":  "operator",
     "auth_password_hash": "pbkdf2-sha256$120000$...",
     "trust_lan":  false
   }
   ```
   `trust_lan: false` matters here — without it, off-LAN clients
   who happen to come from the same public IP as your home would
   bypass auth.
5. **Renewal hook.** certbot renews quarterly via cron; add a deploy
   hook to restart spe-remoted so it picks up the new cert:
   ```bash
   echo '#!/bin/sh
   systemctl restart spe-remoted' | \
       sudo tee /etc/letsencrypt/renewal-hooks/deploy/spe-remote.sh
   sudo chmod +x /etc/letsencrypt/renewal-hooks/deploy/spe-remote.sh
   ```

This works, but every part of it is yours to maintain. Cloudflare
Tunnel removes 4 of the 5 steps above.
