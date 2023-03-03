#!/usr/bin/python3

from mako.template import Template
import sys
import os.path
import os
import re

TEMPLATE = """\
[binaries]
c = '${c}'
ar = '${ar}'
pkgconfig = '/usr/bin/pkg-config'

[built-in options]
c_args = ${c_args}
c_link_args = ${c_link_args}

[host_machine]
system = 'android'
cpu_family = '${cpu_family}'
cpu = '${cpu}'
endian = 'little'
"""

CPU_FAMILIES = [
    (r'i[3456]86$', "x86"),
    (r'x86_64$', "x86_64"),
    (r'aarch64$', "aarch64"),
    (r'arm', "arm"),
]

def get_cpu_family(cpu):
    for regexp, value in CPU_FAMILIES:
        if re.match(regexp, cpu):
            return value

    raise "Unknown cpu {}".format(cpu)

with open('cross.txt', 'wt', encoding='utf-8') as f:
    target = sys.argv[2]
    cpu = target[0:target.find("-")]
    cpu_family = get_cpu_family(cpu)

    target_args = ['-target', sys.argv[2]]
    args_array = target_args + sys.argv[3].split()
    args = "[" + ",".join("'{}'".format(x) for x in args_array) + "]"
    ar = re.sub(r'clang$', "llvm-ar", sys.argv[1])
    print(Template(TEMPLATE).render(c=sys.argv[1],
                                    ar=ar,
                                    c_args=args,
                                    c_link_args=target_args,
                                    cpu_family=cpu_family,
                                    cpu=cpu),
          file=f)
