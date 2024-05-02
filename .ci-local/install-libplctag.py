from __future__ import print_function

import sys, os
import shutil
import subprocess as sp
import zipfile

curdir = os.getcwd()

sys.path.append(os.path.join(curdir, '.ci'))
import cue
cue.detect_context()

sourcedir = os.path.join(cue.homedir, '.source/libplctag')
sdkdir = os.path.join(sourcedir, os.environ['LIBPLCTAG'])
os.environ["LIBPLCTAG_PATH"] = sdkdir
print("SDK path = " + sdkdir)

if 'LIBPLCTAG' in os.environ:
    with open(os.path.join(curdir, 'configure', 'CONFIG_SITE.local'), 'a') as f:
        f.write('LIBPLCTAG = {0}'.format(sdkdir))
    try:
        os.makedirs(sourcedir)
        os.makedirs(cue.toolsdir)
    except:
        pass

    tar_name = '{0}.tar.gz'.format(os.environ['LIBPLCTAG'])
    print('Downloading libplctag {0}'.format(os.environ['LIBPLCTAG']))
    sys.stdout.flush()
    sp.check_call(['curl', '-fsSL', '--retry', '3', '-o', tar_name,
            'https://github.com/libplctag/libplctag/archive/refs/tags/{0}'
            .format(tar_name)],
            cwd=cue.toolsdir)
    
    if os.path.exists(sdkdir):
        shutil.rmtree(sdkdir)
    os.makedirs(sdkdir)
    tar_path = os.path.join(cue.toolsdir, tar_name)
    print("Extracting sdk to " + sdkdir)
    sp.check_call(['tar', '-C', sdkdir, '-xz', '-f', tar_path, '--strip-components', '1'])
    os.remove(tar_path)

    print("Building sdk with cmake")
    build = os.path.join(sdkdir, 'build')
    os.makedirs(build)
    sp.check_call(["cmake", "..", "-DCMAKE_BUILD_TYPE=Release"],cwd=build)
    sp.check_call(["make"],cwd=build)