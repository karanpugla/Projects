import subprocess
import json
import socket
import time

def _enable_backend(name):
    haproxy_cmd = "set server pi_backend/%s state ready" % name
    cmd = "echo %s|nc -U /var/run/haproxy/admin.sock" % haproxy_cmd
    
    try:
        p = subprocess.check_output(cmd,stdin=None, stderr=None, shell=True) 
    except:
        pass

def _is_backend_enabled(name):
    haproxy_cmd = "show stat"
    cmd = "echo %s|nc -U /var/run/haproxy/admin.sock|grep %s" % (haproxy_cmd, name)
    
    try:
        p = subprocess.check_output(cmd,stdin=None, stderr=None, shell=True) 
    except subprocess.CalledProcessError as e:
        p = e.output
        
    status = p.split(',')[17]
    
    if status == 'MAINT' or status == 'DRAIN':
        return False
    else:
        return True
    
def _disable_backend(name):
    haproxy_cmd = "set server pi_backend/%s state drain" % name
    cmd = "echo %s|nc -U /var/run/haproxy/admin.sock" % haproxy_cmd
    
    try:
        p = subprocess.check_output(cmd,stdin=None, stderr=None, shell=True) 
    except:
        pass

def _get_weight(name):
    haproxy_cmd = "get weight pi_backend/%s" % name
    cmd = "echo %s|nc -U /var/run/haproxy/admin.sock|cut -d' ' -f1" % haproxy_cmd
    
    try:
        p = subprocess.check_output(cmd,stdin=None, stderr=None, shell=True)    
        return int(p.strip())
    except subprocess.CalledProcessError as e:
        return int(e.output.strip())
    
def _set_weight(name, w):
    haproxy_cmd = "set weight pi_backend/%s %d" % (name, w)
    cmd = "echo %s|nc -U /var/run/haproxy/admin.sock|cut -d' ' -f1" % haproxy_cmd
    
    try:
        p = subprocess.check_output(cmd,stdin=None, stderr=None, shell=True) 
    except subprocess.CalledProcessError as e:
        pass
    
def _connect_collector(ip, n):
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect((ip, 9090))
    s.sendall(json.dumps(n))
    res = s.recv(2048)
    s.close()
    return json.loads(res) # return list of dict objects

def monitor():
    PiObj = BACKENDS['pi']
    backObj1 = BACKENDS['backend-1']
    backObj2 = BACKENDS['backend-2']
    data = PiObj.get_usage(10)
    cpuData = data[0]['CPU']
    memData = data[0]['MEM']
    print 'cpu=(%d) mem=(%d)' % (cpuData, memData)
    if cpuData > 80 or memData > 80:
        enabled_back1 = backObj1.is_enabled()
        enabled_back2 = backObj2.is_enabled()
        if enabled_back1 and enabled_back2:
            print 'All Backends Already Enabled'
            pi_weight = PiObj.get_weight()
            if pi_weight > 0:
                PiObj.set_weight(pi_weight-5)
                print "current pi_weight", pi_weight, "dropped to", pi_weight-5
            else:
                PiObj.disable()
                print "Disabled Pi"
        elif enabled_back1:
            print 'enabling backend2'
            backObj2.enable()
        else:
            print 'enabling backend1'
            backObj1.enable()
    else:
        last_3_cpu = sum([ d['CPU'] for d in data[0:3] ])/3
        last_3_mem = sum([ d['MEM'] for d in data[0:3] ])/3
        print 'avg last 3 sec: cpu=(%d) mem=(%d)' % (last_3_cpu, last_3_mem)
        if last_3_cpu < 80 and last_3_mem < 80:
            print 'disable backend1 and backend2'
            if backObj1.is_enabled():
                backObj1.disable()
            else: 
                print "Backend 1 already disabled"
            if backObj2.is_enabled():
                backObj2.disable()
            else:
                print "Backend 2 already disabled"
            if PiObj.get_weight() < 20:
                if not PiObj.is_enabled():
                    PiObj.enable()
                PiObj.set_weight(20)
                print "Reset Pi weight"

class Backend:
    """ Available backend names and IPs are:
        name       IP                weight
        pi         127.0.0.1          20
        backend-1  130.245.169.137    80
        backend-2  130.245.169.142    80
    """
    def __init__(self, name, ip):
        self.name = name
        self.ip = ip
    
    def is_enabled(self):
        return _is_backend_enabled(self.name)
        
    def enable(self):
        _enable_backend(self.name)
        
    def disable(self):
        _disable_backend(self.name)
        
    def get_usage(self, n):
        return _connect_collector(self.ip, n)
    
    def get_weight(self):
       w =  _get_weight(self.name)
       return w
        
    def set_weight(self, w):
        _set_weight(self.name, w)
    
BACKENDS = {
            'pi': Backend('pi-0', '127.0.0.1'),
            'backend-1': Backend('backend-1', '130.245.169.137'),
            'backend-2': Backend('backend-2', '130.245.169.142'),
            }

if __name__ == '__main__':
    while True:
        monitor()
        time.sleep(1)
    
    