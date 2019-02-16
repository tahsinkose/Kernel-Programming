#!python3

from socket import *
import sys
import argparse
import re
import random
import time

parser = argparse.ArgumentParser()

parser.add_argument("addr", help="Server address, @path or IP:port")

parser.add_argument("repeat", help="Number of iterations of REQEST/REST",
        default=10, type=int)

parser.add_argument("-r","--request",help="Request length range in millisec. as 10-100", default="600-800")
parser.add_argument("-s","--rest",help="Rest length range in millisec. as 5-55", default="5-55")

try:
    args=parser.parse_args()

    reqst,reqend = tuple(map( int, args.request.split('-',maxsplit=2)))
    restst,restend = tuple(map( int, args.rest.split('-',maxsplit=2)))

    if re.match("@.*",args.addr):
        s = socket(AF_UNIX,SOCK_STREAM)
        s.connect(args.addr[1:])
    else:
        ip,port = args.addr.split(":",maxsplit=2)
        s = socket(AF_INET, SOCK_STREAM)
        s.connect((ip,int(port)))
except Exception as e:
    print(e) 
    parser.print_help()
    sys.exit(1)



for iteration in range(args.repeat):
    requesting = random.randint(reqst,reqend)
    s.send("REQUEST".encode())
    #s.settimeout(requesting/1.0)
    s.settimeout(None)
    print("requesting for {}".format(requesting))
    rest_at = 0
    while True:
        try:
            print("receiving with timeout:", s.gettimeout())
            reply = s.recv(2048)
            rstr = reply.decode()
            print("REPLY:",rstr, end="")
            if re.match(".*assigned.*",rstr):
                start = time.time()
                s.settimeout(requesting/1.0)
                rest_at += 1
                if rest_at == 100:
                    break
            elif re.match(".*removed.*",rstr):
                requesting -= time.time() - start
                s.settimeout(None)
        except timeout:
            break
    s.send("REST".encode())
    resting = random.randint(restst,restend)
    print("resting for {}".format(resting))
    time.sleep(resting)

s.close()