import { RelayRoom } from "./durable/relay-room";
import { config, deviceById } from "./config";
import { verifyDeviceAuth } from "./auth";
import { getFamilies, putFamilies, getConfig } from "./config-store";
import type { MessageMetadata, PostMessagePayload } from "./types";

export { RelayRoom };

const RELAY_INTERNAL = "https://relay.internal";

function bytesToBase64(bytes: Uint8Array): string {
  let binary = "";
  const chunk = 0x8000;
  for (let i = 0; i < bytes.length; i += chunk) {
    binary += String.fromCharCode(...bytes.subarray(i, i + chunk));
  }
  return btoa(binary);
}

async function handlePostMessage(
  request: Request,
  deviceId: string,
  stub: DurableObjectStub
): Promise<Response> {
  const dev = deviceById(deviceId);
  if (!dev) {
    return Response.json({ error: "unknown_device" }, { status: 400 });
  }

  const ct = request.headers.get("content-type") || "";
  if (!ct.includes("multipart/form-data")) {
    return Response.json({ error: "expected_multipart" }, { status: 400 });
  }

  const form = await request.formData();
  const audio = form.get("audio");
  const metaRaw = form.get("metadata");
  if (typeof metaRaw !== "string" || !(audio instanceof File)) {
    return Response.json({ error: "missing_audio_or_metadata" }, { status: 400 });
  }

  let metadata: MessageMetadata;
  try {
    metadata = JSON.parse(metaRaw) as MessageMetadata;
  } catch {
    return Response.json({ error: "invalid_metadata_json" }, { status: 400 });
  }

  const buf = await audio.arrayBuffer();
  const bytes = new Uint8Array(buf);
  if (bytes.byteLength > 128 * 1024) {
    return Response.json({ error: "audio_too_large" }, { status: 413 });
  }

  const toFamilyId = metadata.to_family_id ?? null;
  const broadcast =
    toFamilyId === null ||
    toFamilyId === "" ||
    toFamilyId === "ALL" ||
    toFamilyId === "broadcast";

  const origin = new URL(request.url).origin;

  const payload: PostMessagePayload = {
    from_device_id: deviceId,
    from_family_id: dev.family_id,
    to_family_id: broadcast ? null : toFamilyId,
    broadcast,
    duration_s: typeof metadata.duration_s === "number" ? metadata.duration_s : 0,
    sample_rate_hz:
      typeof metadata.sample_rate_hz === "number" && metadata.sample_rate_hz > 0
        ? metadata.sample_rate_hz
        : 16000,
    audio_base64: bytesToBase64(bytes),
    public_origin: origin,
  };

  return stub.fetch(
    new Request(`${RELAY_INTERNAL}/internal/post-message`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(payload),
    })
  );
}

function checkAdminToken(request: Request, env: Env): boolean {
  const url = new URL(request.url);
  const token = env.ADMIN_TOKEN;
  if (!token) return false;
  return (
    url.searchParams.get("token") === token ||
    request.headers.get("X-Admin-Token") === token
  );
}

function renderAdminPage(families: import("./types").FamilyConfig[]): string {
  const data = JSON.stringify(families);
  return `<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bullerby — Families</title>
<style>
*{box-sizing:border-box}body{font-family:sans-serif;padding:1rem;max-width:40rem;margin:auto}
h2{margin-bottom:.5rem}
table{width:100%;border-collapse:collapse;margin:1rem 0}
th,td{padding:.5rem .6rem;border:1px solid #ddd;text-align:left}
th{background:#f5f5f5;font-weight:600}
input{width:100%;padding:.3rem .4rem;font-size:.95rem;border:1px solid #ccc;border-radius:3px}
button{padding:.4rem .9rem;font-size:.9rem;border:1px solid #999;border-radius:3px;cursor:pointer;background:#fff}
button.danger{color:#c00;border-color:#c00}
button.primary{background:#3366cc;color:#fff;border-color:#3366cc}
#status{margin:.5rem 0;min-height:1.2em;color:#333}
.ok{color:green}.err{color:red}
</style></head><body>
<h2>Bullerby — Edit Families</h2>
<div id="status"></div>
<table id="tbl">
<thead><tr><th>ID</th><th>Name</th><th>Icon</th><th></th></tr></thead>
<tbody id="rows"></tbody>
</table>
<button onclick="addRow()">+ Add family</button>
&nbsp;
<button class="primary" onclick="save()">Save</button>
<script>
const TOKEN = new URLSearchParams(location.search).get('token') || '';
let families = ${data};

function render(){
  const tbody = document.getElementById('rows');
  tbody.innerHTML = '';
  families.forEach((f,i) => {
    const tr = document.createElement('tr');
    tr.innerHTML =
      '<td><input value="'+esc(f.id)+'" onchange="families['+i+'].id=this.value"></td>'+
      '<td><input value="'+esc(f.name)+'" onchange="families['+i+'].name=this.value"></td>'+
      '<td><input value="'+esc(f.icon)+'" maxlength="2" style="width:3rem" onchange="families['+i+'].icon=this.value"></td>'+
      '<td><button class="danger" onclick="remove('+i+')">✕</button></td>';
    tbody.appendChild(tr);
  });
}

function esc(s){return String(s).replace(/&/g,'&amp;').replace(/"/g,'&quot;');}

function addRow(){
  families.push({id:'family-'+Date.now(),name:'',icon:''});
  render();
}

function remove(i){families.splice(i,1);render();}

async function save(){
  const st=document.getElementById('status');
  st.textContent='Saving…';st.className='';
  const res = await fetch('/api/admin/families?token='+encodeURIComponent(TOKEN),{
    method:'PUT',headers:{'Content-Type':'application/json'},
    body:JSON.stringify(families)
  });
  const j = await res.json();
  if(res.ok){families=j.families;render();st.textContent='Saved ✓';st.className='ok';}
  else{st.textContent='Error: '+j.error;st.className='err';}
}

render();
</script></body></html>`;
}

export default {
  async fetch(request: Request, env: Env): Promise<Response> {
    const url = new URL(request.url);
    const relayId = env.RELAY.idFromName("global");
    const stub = env.RELAY.get(relayId);

    if (url.pathname === "/health" || url.pathname === "/") {
      return Response.json({
        ok: true,
        service: "bullerby-chat",
        api: {
          ws: "/api/ws",
          config: "GET /api/devices/{id}/config",
          messages: "POST /api/messages",
        },
      });
    }

    if (url.pathname === "/api/ws") {
      const auth = verifyDeviceAuth(request, config);
      if (!auth) {
        return new Response("Unauthorized", { status: 401 });
      }
      return stub.fetch(request);
    }

    if (url.pathname === "/api/devices/register" && request.method === "POST") {
      const auth = verifyDeviceAuth(request, config);
      if (!auth) {
        return new Response("Unauthorized", { status: 401 });
      }
      const dev = deviceById(auth.deviceId);
      return Response.json({
        ok: true,
        device_id: auth.deviceId,
        family_id: dev?.family_id,
      });
    }

    const configMatch = /^\/api\/devices\/([^/]+)\/config$/.exec(url.pathname);
    if (configMatch && request.method === "GET") {
      const auth = verifyDeviceAuth(request, config);
      if (!auth) {
        return new Response("Unauthorized", { status: 401 });
      }
      const requestedId = decodeURIComponent(configMatch[1]!);
      if (requestedId !== auth.deviceId) {
        return new Response("Forbidden", { status: 403 });
      }
      const dev = deviceById(auth.deviceId);
      if (!dev) {
        return new Response("Not found", { status: 404 });
      }
      const liveConfig = await getConfig(env.CONFIG);
      return Response.json({
        device_id: dev.id,
        family_id: dev.family_id,
        families: liveConfig.families,
      });
    }

    if (url.pathname === "/api/messages" && request.method === "POST") {
      const auth = verifyDeviceAuth(request, config);
      if (!auth) {
        return new Response("Unauthorized", { status: 401 });
      }
      return handlePostMessage(request, auth.deviceId, stub);
    }

    const audioMatch = /^\/api\/messages\/([^/]+)\/audio$/.exec(url.pathname);
    if (audioMatch && request.method === "GET") {
      const messageId = decodeURIComponent(audioMatch[1]!);
      return stub.fetch(
        new Request(
          `${RELAY_INTERNAL}/internal/audio/${encodeURIComponent(messageId)}`,
          { method: "GET" }
        )
      );
    }

    // Admin: serve editor page
    if (url.pathname === "/admin" && request.method === "GET") {
      if (!checkAdminToken(request, env)) {
        return new Response("Unauthorized", { status: 401 });
      }
      const families = await getFamilies(env.CONFIG);
      return new Response(renderAdminPage(families), {
        headers: { "Content-Type": "text/html; charset=utf-8" },
      });
    }

    // Admin: save families
    if (url.pathname === "/api/admin/families" && request.method === "PUT") {
      if (!checkAdminToken(request, env)) {
        return new Response("Unauthorized", { status: 401 });
      }
      let body: unknown;
      try {
        body = await request.json();
      } catch {
        return Response.json({ error: "invalid json" }, { status: 400 });
      }
      try {
        const saved = await putFamilies(env.CONFIG, body);
        return Response.json({ ok: true, families: saved });
      } catch (e: unknown) {
        return Response.json({ error: String(e) }, { status: 400 });
      }
    }

    return new Response("Not found", { status: 404 });
  },
} satisfies ExportedHandler<Env>;
