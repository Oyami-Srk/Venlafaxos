#!/usr/local/bin/python3
# -%- coding: utf-8 -%-
# -%- author: Shiroko <hhx.xxm@gmail.com> -%-
"""
To generate modules.h from all .c files in given dir (will be src/kernel/modules/)
"""

import time
import re
import yaml

template: str = """// {info}
extern void {fn}(void);\n"""


def parse_c_file(fn: str) -> dict:
    yaml_str = ""
    with open(fn, "r") as f:
        s = f.read()
        regx = re.compile(r'/\*([\w\W]*?)\*/')
        yaml_str = regx.findall(s)[0]
    return yaml.load(yaml_str, Loader=yaml.FullLoader)


def generate_from_yaml(obj: dict) -> str:
    if ("module" not in obj.keys()):
        return ""
    info: dict = obj["module"]
    return info


def iter_c_files_in_dir(dir: str):
    import os
    files = os.listdir(dir)
    modules = []
    for f in files:
        if not os.path.isdir(f):
            if os.path.splitext(f)[-1] == ".c":
                try:
                    modules.append(
                        generate_from_yaml(parse_c_file(dir + '/' + f)))
                except Exception as e:
                    import sys
                    print(e, file=sys.stderr)
                    exit(1)
    return modules


def generate_header_file(fn: str, modules: list, dryrun: bool):
    l = []
    meet_1 = False
    meet_2 = False
    wrote = False

    content = '\n'

    for info in modules:
        content += template.format(info=info["summary"], fn=info["entry"])

    content += "\n#define __MODULES_COUNT__ %d\n" % (len(modules))
    content += "#define __MODULES_ENTRIES__ {%s}\n" % (','.join(
        map(lambda x: "(unsigned int)" + x['entry'], modules)))
    content += "#define __MODULES_PREFERRED_PID__ {%s}\n\n" % (','.join(
        map(
            lambda x: "(unsigned int)" + str(
                x.get('preferred_pid', '0xFFFFFFFE')), modules)))

    if dryrun:
        with open(fn, "r") as f:
            l = f.readlines()
            for line in l:
                if meet_1 == meet_2 or "{Insert Above}" in line:
                    print(line, end='')
                elif meet_1 == True and wrote == False:
                    print(content, end='')
                    wrote = True
                if "{Insert Below}" in line:
                    meet_1 = True
                elif "{Insert Above}" in line:
                    meet_2 = True
    else:
        with open(fn, "r") as f:
            l = f.readlines()
        with open(fn, "w") as f:
            for line in l:
                if meet_1 == meet_2 or "{Insert Above}" in line:
                    f.write(line)
                elif meet_1 == True and wrote == False:
                    f.write(content)
                    wrote = True
                if "{Insert Below}" in line:
                    meet_1 = True
                elif "{Insert Above}" in line:
                    meet_2 = True


if __name__ == "__main__":
    import argparse
    parser = argparse.ArgumentParser(
        description=
        "Automaticly generate function protos from .c files's definitions")
    parser.add_argument('-m',
                        '--modules',
                        dest='MODS',
                        help="dir contains .c files")
    parser.add_argument('-a',
                        '--header',
                        dest='HEADER',
                        help='header template')
    parser.add_argument('-D',
                        '--dryrun',
                        dest='DRYRUN',
                        help='output only',
                        action='store_true')
    args = parser.parse_args()
    generate_header_file(args.HEADER, iter_c_files_in_dir(args.MODS),
                         args.DRYRUN)
