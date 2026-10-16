"""Range-capable static fixture server for realistic browser seek tests."""
import http.server
from pathlib import Path
import re
class MediaHandler(http.server.SimpleHTTPRequestHandler):
    def send_head(self):
        path=Path(self.translate_path(self.path))
        if not path.is_file():return super().send_head()
        source=path.open('rb');size=path.stat().st_size;start=0;end=size-1
        value=self.headers.get('Range')
        if value:
            match=re.fullmatch(r'bytes=(\d*)-(\d*)',value)
            if not match or not any(match.groups()):source.close();self.send_error(416);return None
            first,last=match.groups()
            if first:start=int(first);end=int(last) if last else size-1
            else:start=max(0,size-int(last))
            if start>=size or end<start:source.close();self.send_error(416);return None
            end=min(end,size-1)
        self.send_response(206 if value else 200)
        self.send_header('Content-Type',self.guess_type(str(path)));self.send_header('Accept-Ranges','bytes')
        self.send_header('Content-Length',str(end-start+1))
        if value:self.send_header('Content-Range',f'bytes {start}-{end}/{size}')
        self.end_headers();source.seek(start);self.remaining=end-start+1;return source
    def copyfile(self,source,output):
        if not hasattr(self,'remaining'):return super().copyfile(source,output)
        while self.remaining:
            block=source.read(min(self.remaining,65536))
            if not block:break
            output.write(block);self.remaining-=len(block)
