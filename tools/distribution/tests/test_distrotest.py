from unittest.mock import AsyncMock, PropertyMock

import pytest

from tools.base import checker
from tools.distribution import distrotest


@pytest.mark.parametrize("stream", [None, "STREAM"])
def test_image_constructor(patches, stream):
    args = ("DOCKER", "PATH", "BUILD_IMAGE", "BUILD_TAG", "TESTFILE", "DISTRO")
    if stream is not None:
        args += (stream, )
    image = distrotest.DistroTestImage(*args)
    assert image.docker == "DOCKER"
    assert image.path == "PATH"
    assert image.build_image == "BUILD_IMAGE"
    assert image.build_tag == "BUILD_TAG"
    assert image.testfile == "TESTFILE"
    assert image.distro == "DISTRO"
    assert image.stream == stream


def test_image_dockerfile(patches):
    image = distrotest.DistroTestImage("DOCKER", "PATH", "BUILD_IMAGE", "BUILD_TAG", "TESTFILE", "DISTRO")
    patched = patches(
        ("DistroTestImage.build_command", dict(new_callable=PropertyMock)),
        ("DistroTestImage.build_env", dict(new_callable=PropertyMock)),
        ("DistroTestImage.dockerfile_template", dict(new_callable=PropertyMock)),
        ("DistroTestImage.install_dir", dict(new_callable=PropertyMock)),
        ("DistroTestImage.mount_install_dir", dict(new_callable=PropertyMock)),
        ("DistroTestImage.mount_testfile_path", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as patchy:
        m_command, m_env, m_template, m_install, m_minstall, m_tpath = patchy
        assert image.dockerfile == m_template.return_value.format.return_value

    assert (
        list(m_template.return_value.format.call_args)
        == [(),
            {'distro': 'BUILD_IMAGE',
             'tag': 'BUILD_TAG',
             'install_dir': m_install.return_value,
             'env': m_env.return_value,
             'install_mount_path': m_minstall.return_value,
             'test_filename': 'TESTFILE',
             'test_mount_path': m_tpath.return_value,
             'build_command': m_command.return_value}])


def test_image_tag(patches):
    image = distrotest.DistroTestImage("DOCKER", "PATH", "BUILD_IMAGE", "BUILD_TAG", "TESTFILE", "DISTRO")
    assert image.tag == f"DISTRO:latest"


def test_image_testfile_path(patches):
    image = distrotest.DistroTestImage("DOCKER", "PATH", "BUILD_IMAGE", "BUILD_TAG", "TESTFILE", "DISTRO")
    patched = patches(
        "os",
        prefix="tools.distribution.distrotest")

    with patched as (m_os, ):
        assert image.testfile_path == m_os.path.join.return_value

    assert (
        list(m_os.path.basename.call_args)
        == [('TESTFILE',), {}])
    assert (
        list(m_os.path.join.call_args)
        == [('PATH', m_os.path.basename.return_value), {}])



def __test_distrotest_constructor(patches):
    check = checker.Checker()
    dtest = distrotest.DistroTest(check, "PATH", "INSTALLABLE", "NAME", "IMAGE", "TAG")
    patched = patches(
        ("checker.Checker.stdout", dict(new_callable=PropertyMock)),
        ("DistroChecker.docker", dict(new_callable=PropertyMock)),
        ("DistroChecker.log", dict(new_callable=PropertyMock)),
        ("DistroChecker.testfile", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_out, m_docker, m_log, m_test):
        assert dtest.checker == checker
        assert dtest.stdout == m_out.return_value
        assert dtest.docker == m_docker.return_value
        assert dtest.log == m_log.return_value
        assert dtest.testfile == m_test.return_value
        assert dtest.path == "PATH"
        assert dtest.installable == "INSTALLABLE"
        assert dtest.distro == "NAME"
        assert dtest.build_image == "IMAGE"
        assert dtest.build_tag == "TAG"


def test_distrotest_config(patches):
    check = checker.Checker()
    dtest = distrotest.DistroTest(check, "PATH", "INSTALLABLE", "NAME", "IMAGE", "TAG")
    assert dtest.config == dict(Image="NAME")


def test_distrotest_package_name(patches):
    check = checker.Checker()
    dtest = distrotest.DistroTest(check, "PATH", "INSTALLABLE", "NAME", "IMAGE", "TAG")
    patched = patches(
        ("DistroTest.package_filename", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_name,):
        assert dtest.package_name == m_name.return_value.split.return_value.__getitem__.return_value

    assert (
        list(m_name.return_value.split.call_args)
        == [('_',), {}])
    assert (
        list(m_name.return_value.split.return_value.__getitem__.call_args)
        == [(0,), {}])


def test_distrotest_test_cmd(patches):
    check = checker.Checker()
    dtest = distrotest.DistroTest(check, "PATH", "INSTALLABLE", "NAME", "IMAGE", "TAG")
    patched = patches(
        ("DistroTest.image", dict(new_callable=PropertyMock)),
        ("DistroTest.package_name", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_image, m_name):
        assert dtest.test_cmd == (
            m_image.return_value.mount_testfile_path,
            m_image.return_value.installable_path.return_value,
            m_name.return_value,
            "NAME")
    assert (
        list(m_image.return_value.installable_path.call_args)
        == [('INSTALLABLE',), {}])


@pytest.mark.asyncio
@pytest.mark.parametrize("exists", [True, False])
async def test_distrotest_build(patches, exists):
    check = checker.Checker()
    dtest = distrotest.DistroTest(check, "PATH", "INSTALLABLE", "NAME", "IMAGE", "TAG")
    patched = patches(
        ("DistroTest.image", dict(new_callable=PropertyMock)),
        ("DistroTest.log", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.distrotest")

    with patched as (m_image, m_log):
        m_image.return_value.exists = AsyncMock(return_value=exists)
        m_image.return_value.build = AsyncMock()
        assert not await dtest.build()

    assert (
        list(m_image.return_value.exists.call_args)
        == [(), {}])

    if exists:
        assert not m_log.called
        assert not m_image.return_value.build.called
        return

    assert (
        list(m_image.return_value.build.call_args)
        == [(), {}])
    assert (
        list(list(c) for c in m_log.return_value.info.call_args)
        == [['[NAME] Image built'], []])


def test_distrotest_package_filename(patches):
    check = checker.Checker()
    dtest = distrotest.DistroTest(check, "PATH", "INSTALLABLE", "NAME", "IMAGE", "TAG")
    patched = patches(
        "os",
        prefix="tools.distribution.distrotest")

    with patched as (m_os,):
        assert dtest.package_filename == m_os.path.basename.return_value

    assert (
        list(m_os.path.basename.call_args)
        == [('INSTALLABLE',), {}])
