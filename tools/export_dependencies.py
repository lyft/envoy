#!/usr/bin/env python

import sys
import os
from jinja2 import Template
from tools.dependency import utils as dep_utils

EXCLUDED_DEPS = (
    # URL of kafka_server_binary returns 404.
    "kafka_server_binary",)

template_string = """# DO NOT EDIT. This file is generated by tools/export_dependencies.py.
load("//bazel:distdir.bzl", "distdir_tar")

def distfiles_imports():
    distdir_tar(
        name = "distfiles",
        dirname = "{{ dirname }}",
        archives = [
            {%- for item in archives %}
            "{{ item }}",
            {%- endfor %}
        ],
        urls = {
            {%- for k, v in urls.items() %}
            "{{ k }}": [
                {%- for each in v %}
                "{{ each }}",
                {%- endfor %}
            ],
            {%- endfor %}
        },
        sha256 = {
            {%- for k, v in sha256.items() %}
            "{{ k }}": "{{ v }}",
            {%- endfor %}
        },
    )

"""

if __name__ == '__main__':
    output_path = None
    if len(sys.argv) > 1:
        output_path = sys.argv[1]
    else:
        output_path = "bazel/distfiles.bzl"

    archives = []
    sha256 = {}
    urls = {}
    dirname = "distdir"

    for k, v in dep_utils.RepositoryLocations().items():
        if k in EXCLUDED_DEPS:
            continue

        # Archive file name. Bazel lookup files in distdir using the last part of url path.
        # https://github.com/bazelbuild/bazel/blob/4.0.0/src/main/java/com/google/devtools/build/lib/bazel/repository/downloader/DownloadManager.java#L113
        output = v["urls"][0].rsplit("/", 1)[-1]
        if "output" in v:
            output = v["output"]
        archives.append(output)
        sha256[output] = v["sha256"]
        urls[output] = v["urls"]

    template = Template(template_string)
    render_result = template.render(archives=archives, sha256=sha256, urls=urls, dirname=dirname)

    file = open(output_path, "w")
    file.write(render_result)
    file.close()
