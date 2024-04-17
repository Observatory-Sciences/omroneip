from __future__ import print_function

import sys, os
import shutil
import subprocess as sp
import zipfile

curdir = os.getcwd()

sys.path.append(os.path.join(curdir, '.ci'))
import cue
cue.detect_context()

sourcedir = os.path.join(cue.homedir, '.source')
sdkdir = os.path.join(sourcedir, 'sdk')

if 'LIBPLCTAG' in os.environ:
    with open(os.path.join(curdir, 'configure', 'CONFIG_SITE.local'), 'a') as f:
        f.write('LIBPLCTAG = {0}'.format(sdkdir))
    try:
        os.makedirs(sourcedir)
        os.makedirs(cue.toolsdir)
    except:
        pass

    zip_name = '{0}.zip'.format(os.environ['LIBPLCTAG'])
    print('Downloading libplctag {0}'.format(os.environ['LIBPLCTAG']))
    sys.stdout.flush()
    sp.check_call(['curl', '-fsSL', '--retry', '3', '-o', zip_name,
            'https://github.com/libplctag/libplctag/archive/refs/tags/{0}'
            .format(zip_name)],
            cwd=cue.toolsdir)
    
    if os.path.exists(sdkdir):
        shutil.rmtree(sdkdir)
    zip_path = os.path.join(cue.toolsdir, zip_name)
    with zipfile.ZipFile(zip_path, 'r') as zip_ref:
        zip_ref.extractall(sdkdir)
    os.remove(zip_path)