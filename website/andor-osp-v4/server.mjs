import http from "node:http";
import { readFile } from "node:fs/promises";
import { dirname, extname, posix } from "node:path";
import { fileURLToPath } from "node:url";
import { unzipSync, strFromU8 } from "fflate";

const root = dirname(fileURLToPath(import.meta.url));
const types = {".html":"text/html; charset=utf-8",".css":"text/css; charset=utf-8",".js":"text/javascript; charset=utf-8",".webp":"image/webp",".avif":"image/avif",".png":"image/png",".jpg":"image/jpeg",".jpeg":"image/jpeg",".svg":"image/svg+xml",".mp4":"video/mp4",".webm":"video/webm",".woff":"font/woff",".woff2":"font/woff2"};
let archive = {};
let ready = false;
try {
  const zip = unzipSync(new Uint8Array(await readFile(new URL("./site.zip",import.meta.url))));
  for (const [name,bytes] of Object.entries(zip)) {
    if (name.endsWith("/") || !name.startsWith("andor-osp-v4/")) continue;
    const rel = name.slice("andor-osp-v4/".length);
    if (!rel || rel.split("/").includes("..")) continue;
    archive[rel] = bytes;
  }
  ready = Boolean(archive["index.html"] && archive["style.css"]);
  console.log(ready ? "Loaded ANDOR V4 source archive" : "V4 ZIP missing required site files");
} catch (error) {
  console.log("V4 site.zip pending upload:",error.message);
}
const pending = '<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>ANDOR V4 — Upload pending</title></head><body style="font:16px system-ui;background:#100d0c;color:#e8e3da;margin:12vh auto;max-width:580px;padding:24px"><h1>ANDOR OSP V4</h1><p>The preview service is ready. The exact V4 site.zip asset package has not been uploaded yet.</p></body></html>';
const server = http.createServer((req,res)=>{
  let requestPath;
  try { requestPath = decodeURIComponent(new URL(req.url,"http://localhost").pathname); } catch { res.writeHead(400);res.end();return; }
  const p = requestPath === "/" ? "index.html" : requestPath.replace(/^\/+/, "");
  if (p !== posix.normalize(p) || p.startsWith("../") || p.includes("\0")) { res.writeHead(403);res.end("Forbidden");return; }
  if (!ready){ res.writeHead(503,{"Content-Type":"text/html; charset=utf-8","Cache-Control":"no-store"});res.end(pending);return; }
  const bytes = archive[p];
  if (!bytes){res.writeHead(404);res.end("Not found");return;}
  res.writeHead(200,{"Content-Type":types[extname(p)]??"application/octet-stream","Cache-Control":p.startsWith("assets/")?"public, max-age=3600":"no-cache"});
  res.end(Buffer.from(bytes));
});
server.listen(Number(process.env.PORT ?? 3000),"0.0.0.0",()=>console.log("ANDOR OSP V4 preview listening"));
