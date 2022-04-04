#!/usr/bin/env python
# coding: utf8

import os
import re
import sys
#可以考虑使用图形化界面选择文件夹路径
path='E:\\GitHub\\adinxu.github.io\\_posts'
def newname(olddirname):
    return re.sub("^\d\d\d\d-\d\d-\d\d",'2022-04-02',olddirname)

if __name__=='__main__':
    print(f'current dir is {os.listdir(os.getcwd())}')
    os.chdir(path)
    print(f'current dir is {os.listdir(os.getcwd())}')
    flist=os.listdir()
    for f in flist:
        os.rename(f,newname(f))
