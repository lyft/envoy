from unittest.mock import AsyncMock, PropertyMock

import pytest

from tools.base.checker import Checker
from tools.distribution import verify


def test_checker_constructor(patches):
    checker = verify.DistroChecker("path1", "path2", "path3")
    assert isinstance(checker, Checker)

    patched = patches(
        ("DistroChecker.args", dict(new_callable=PropertyMock)),
        prefix="tools.distribution.verify")

    with patched as (m_args, ):
        assert checker.config == m_args.return_value.config
        assert checker.packages_tarball == m_args.return_value.packages
        assert checker.testfile == m_args.return_value.testfile
        assert checker.test_distributions == m_args.return_value.distribution
        assert checker.distro_test_class == verify.DistroTest


def test_checker_docker(patches):
    checker = verify.DistroChecker("path1", "path2", "path3")
    patched = patches(
        "aiodocker",
        prefix="tools.distribution.verify")

    with patched as (m_docker, ):
        assert checker.docker == m_docker.Docker.return_value

    assert (
        list(m_docker.Docker.call_args)
        == [(), {}])
    assert "docker" in checker.__dict__


def test_checker_tempdir(patches):
    checker = verify.DistroChecker("path1", "path2", "path3")
    patched = patches(
        "tempfile",
        prefix="tools.distribution.verify")

    with patched as (m_temp, ):
        assert checker.tempdir == m_temp.TemporaryDirectory.return_value

    assert (
        list(m_temp.TemporaryDirectory.call_args)
        == [(), {}])
    assert "tempdir" in checker.__dict__
