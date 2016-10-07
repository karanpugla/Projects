#!/usr/bin/python
import subprocess
import SocketServer
import json
import signal

MAX_RESOURCE_RECORDS = 60
PORT = 9090

_collector_instance = None

class _ResourceUsageListener(SocketServer.TCPServer):
    
    def __init__(self, server_address, req_handler, timeout=0.5):
        SocketServer.TCPServer.timeout = timeout
        SocketServer.TCPServer.allow_reuse_address = True
        SocketServer.TCPServer.__init__(self, server_address, req_handler)
        
        
class _ResourceReqhandler(SocketServer.BaseRequestHandler):
     
    def handle(self):
        global _collector_instance
        data = self.request.recv(1024)
        print 'req recvd', data, 'pos=', _collector_instance.resource_usage.pos
        n = int(json.loads(data))
        ret = _collector_instance.resource_usage.get(n)
        if ret is None:
            resp = None
        else:
            resp = ret
        
        resp = json.dumps(resp)
        self.request.sendall(resp)

class _ResourceUsage:
    def __init__(self):
        self.pos = 0
        self.buffer = [ None for i in range(MAX_RESOURCE_RECORDS) ]
        
    def put(self, u):
        i = self.pos % MAX_RESOURCE_RECORDS
        self.buffer[i] = u
        self.pos = i + 1
        
    def get(self, n):
        if n > MAX_RESOURCE_RECORDS:
            return None
        res = []
        index = self.pos - 1 
        for i in range(n):
            print 'index=', index
            if self.buffer[index] is None:
                return None
            else:
                res.append(self.buffer[index])
                index -= 1
        
        return res  # List of dict objects
            
        
class Collector:
    def __init__(self):
        global _collector_instance 
        _collector_instance = self
        self.resource_usage = _ResourceUsage()
        self.process = subprocess.Popen([ '/usr/bin/sar', '-r', '-u', '1'] , shell=False,
                                         stdout=subprocess.PIPE)
        self.server = _ResourceUsageListener(('', PORT), _ResourceReqhandler)
        
    def stop(self):
        print 'Good bye'
        self.server.server_close()
        self.process.kill()
        exit(0)
        
    def run(self):
        lines = dict(CPU=None, MEM=None)
        while True:
            ru = dict(CPU=None, MEM=None)
            while self.process.stdout.readline().find('idle') < 0:
                continue
    
            lines['CPU'] = self.process.stdout.readline()
            self.process.stdout.readline();
            self.process.stdout.readline()
            lines['MEM'] = self.process.stdout.readline()
            
            ru['CPU']  = 100 - float(lines['CPU'].split()[8])
            ru['MEM']  = float(lines['MEM'].split()[4])
            print ru
            
            self.resource_usage.put(ru)
            self.server.handle_request()
            
            
def cleanup(signum, frame):
    _collector_instance.stop()             
            
if __name__ == "__main__":
    c = Collector()
    signal.signal(signal.SIGTERM, cleanup)
    signal.signal(signal.SIGINT,  cleanup)
    print 'Starting'
    c.run()
    
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
        