import http from "node:http";
import { readFile, stat } from "node:fs/promises";
import { dirname, extname, join, normalize } from "node:path";
import { fileURLToPath } from "node:url";

const root = dirname(fileURLToPath(import.meta.url));
const types = {".html":"text/html; charset=utf-8",".css":"text/css; charset=utf-8",".js":"text/javascript; charset=utf-8",".webp":"image/webp",".avif":"image/avif",".png":"image/png",".jpg":"image/jpeg",".svg":"image/svg+xml",".mp4":"video/mp4",".webm":"video/webm"};
const server = http.createServer(async (req,res)=>{
  const requestPath = decodeURIComponent(new URL(req.url,"http://localhost").pathname);
  const path = normalize(requestPath === "/" ? "/index.html" : requestPath).replace(/^[/\\]+/,"");
  const file = join(root,path);
  if (!(file === root || file.startsWith(root + "/"))){res.writeHead(403);res.end("Forbidden");return;}
  try {
    const info = await stat(file);
    if (!info.isFile()) throw Error("Not a file");
    res.writeHead(200,{"Content-Type":types[extname(file)] ?? "application/octet-stream","Cache-Control":path.includes("/assets/")?"public, max-age=3600":"no-cache"});
    res.end(await readFile(file));
  } catch { res.writeHead(404);res.end("Not found"); }
});
server.listen(Number(process.env.PORT ?? 3000),"0.0.0.0",()=>console.log("ANDOR OSP V4 preview listening"));
