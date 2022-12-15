# gitlab.py - helpers for generating CI rules from templates
#
# Copyright (C) 2021 Red Hat, Inc.
#
# SPDX-License-Identifier: GPL-2.0-or-later

import textwrap

#
# The job templates in this file rely on variables in a
# number of namespaces
#
#  - CI_nnn - standard variables defined by GitLab
#
#  - CIRRUS_nnn - variables for controlling Cirrus CI
#    job integration
#
#  - RUN_nnn - variables for a maintainer to set when
#    triggering a  pipeline
#
#  - JOB_nnn - variables set against jobs which influence
#    rules in templates they inherit from
#


def docs():
    return textwrap.dedent(
        """
        # Variables that can be set to control the behaviour of
        # pipelines that are run
        #
        #  - RUN_ALL_CONTAINERS - build all containers
        #    even if they don't have any changes detected
        #
        # These can be set as git push options
        #
        #  $ git push -o ci.variable=RUN_ALL_CONTAINERS=1
        #
        # Aliases can be set for common usage
        #
        #  $ git config --local alias.push-all-ctr "push -o ci.variable=RUN_ALL_CONTAINERS=1"
        #
        # Allowing the less verbose invocation
        #
        #  $ git push-all-ctr
        #
        # Pipeline variables can also be set in the repository
        # pipeline config globally, or set against scheduled pipelines
        """)


def includes(paths):
    lines = [f"  - local: '/{path}'" for path in paths]
    return "include:\n" + "\n".join(lines)


def format_variables(variables):
    job = []
    for key in sorted(variables.keys()):
        val = variables[key]
        job.append(f"    {key}: {val}")
    if len(job) > 0:
        return "  variables:\n" + "\n".join(job) + "\n"
    return ""


def container_template(namespace, project, cidir):
    return textwrap.dedent(
        f"""
        # For upstream
        #
        #   - Push to default branch:
        #       -> rebuild if dockerfile changed, no cache
        #   - Otherwise
        #       -> rebuild if RUN_ALL_CONTAINERS=1, no cache,
        #          to pick up new published distro packages or
        #          recover from deleted tag
        #
        # For forks
        #   - Always rebuild, with cache
        #
        .container_job:
          image: docker:stable
          stage: containers
          needs: []
          services:
            - docker:dind
          before_script:
            - export TAG="$CI_REGISTRY_IMAGE/ci-$NAME:latest"
            - export COMMON_TAG="$CI_REGISTRY/{namespace}/{project}/ci-$NAME:latest"
            - docker info
            - docker login "$CI_REGISTRY" -u "$CI_REGISTRY_USER" -p "$CI_REGISTRY_PASSWORD"
          script:
            - if test $CI_PROJECT_NAMESPACE = "{namespace}";
              then
                docker build --tag "$TAG" -f "{cidir}/containers/$NAME.Dockerfile" {cidir}/containers ;
              else
                docker pull "$TAG" || docker pull "$COMMON_TAG" || true ;
                docker build --cache-from "$TAG" --cache-from "$COMMON_TAG" --tag "$TAG" -f "{cidir}/containers/$NAME.Dockerfile" {cidir}/containers ;
              fi
            - docker push "$TAG"
          after_script:
            - docker logout
          rules:
            - if: '$CI_PIPELINE_SOURCE == "merge_request_event"'
              when: never
            - if: '$CI_PROJECT_NAMESPACE == "{namespace}" && $CI_PIPELINE_SOURCE == "push" && $CI_COMMIT_BRANCH == $CI_DEFAULT_BRANCH'
              when: on_success
              changes:
               - {cidir}/gitlab/container-templates.yml
               - {cidir}/containers/$NAME.Dockerfile
            - if: '$CI_PROJECT_NAMESPACE == "{namespace}" && $RUN_ALL_CONTAINERS == "1"'
              when: on_success
            - if: '$CI_PROJECT_NAMESPACE == "{namespace}"'
              when: never
            - if: '$JOB_OPTIONAL'
              when: manual
              allow_failure: true
            - when: on_success
        """)


def _build_template(template, image):
    return textwrap.dedent(
        f"""
        {template}:
          image: $CI_REGISTRY_IMAGE/{image}:latest
          stage: builds
          rules:
            - if: '$CI_PIPELINE_SOURCE == "merge_request_event"'
              when: never
            - if: '$JOB_OPTIONAL'
              when: manual
              allow_failure: true
            - when: on_success
        """)


def native_build_template():
    return _build_template(".gitlab_native_build_job",
                           "ci-$NAME")


def cross_build_template():
    return _build_template(".gitlab_cross_build_job",
                           "ci-$NAME-cross-$CROSS")


def cirrus_template(cidir):
    return textwrap.dedent(
        f"""
        .cirrus_build_job:
          stage: builds
          image: registry.gitlab.com/libvirt/libvirt-ci/cirrus-run:master
          needs: []
          script:
            - source {cidir}/cirrus/$NAME.vars
            - sed -e "s|[@]CI_REPOSITORY_URL@|$CI_REPOSITORY_URL|g"
                  -e "s|[@]CI_COMMIT_REF_NAME@|$CI_COMMIT_REF_NAME|g"
                  -e "s|[@]CI_COMMIT_SHA@|$CI_COMMIT_SHA|g"
                  -e "s|[@]CIRRUS_VM_INSTANCE_TYPE@|$CIRRUS_VM_INSTANCE_TYPE|g"
                  -e "s|[@]CIRRUS_VM_IMAGE_SELECTOR@|$CIRRUS_VM_IMAGE_SELECTOR|g"
                  -e "s|[@]CIRRUS_VM_IMAGE_NAME@|$CIRRUS_VM_IMAGE_NAME|g"
                  -e "s|[@]UPDATE_COMMAND@|$UPDATE_COMMAND|g"
                  -e "s|[@]UPGRADE_COMMAND@|$UPGRADE_COMMAND|g"
                  -e "s|[@]INSTALL_COMMAND@|$INSTALL_COMMAND|g"
                  -e "s|[@]PATH@|$PATH_EXTRA${{PATH_EXTRA:+:}}\\$PATH|g"
                  -e "s|[@]PKG_CONFIG_PATH@|$PKG_CONFIG_PATH|g"
                  -e "s|[@]PKGS@|$PKGS|g"
                  -e "s|[@]MAKE@|$MAKE|g"
                  -e "s|[@]PYTHON@|$PYTHON|g"
                  -e "s|[@]PIP3@|$PIP3|g"
                  -e "s|[@]PYPI_PKGS@|$PYPI_PKGS|g"
                  -e "s|[@]XML_CATALOG_FILES@|$XML_CATALOG_FILES|g"
              <{cidir}/cirrus/build.yml >{cidir}/cirrus/$NAME.yml
            - cat {cidir}/cirrus/$NAME.yml
            - cirrus-run -v --show-build-log always {cidir}/cirrus/$NAME.yml
          rules:
            - if: '$CIRRUS_GITHUB_REPO == null || $CIRRUS_API_TOKEN == null'
              when: never
            - if: '$JOB_OPTIONAL'
              when: manual
              allow_failure: true
            - when: on_success
        """)


def check_dco_job(namespace):
    jobvars = {
        "GIT_DEPTH": "1000",
    }
    return textwrap.dedent(
        f"""
        check-dco:
          stage: sanity_checks
          needs: []
          image: registry.gitlab.com/libvirt/libvirt-ci/check-dco:master
          script:
            - /check-dco {namespace}
          except:
            variables:
              - $CI_PROJECT_NAMESPACE == '{namespace}'
        """) + format_variables(jobvars)


def cargo_fmt_job():
    return textwrap.dedent(
        """
        cargo-fmt:
          stage: sanity_checks
          image: registry.gitlab.com/libvirt/libvirt-ci/cargo-fmt:master
          needs: []
          script:
            - /cargo-fmt
          artifacts:
            paths:
              - cargo-fmt.txt
            expire_in: 1 week
            when: on_failure
        """)


def go_fmt_job():
    return textwrap.dedent(
        """
        go-fmt:
          stage: sanity_checks
          image: registry.gitlab.com/libvirt/libvirt-ci/go-fmt:master
          needs: []
          script:
            - /go-fmt
          artifacts:
            paths:
              - go-fmt.patch
            expire_in: 1 week
            when: on_failure
        """)


def clang_format_job():
    return textwrap.dedent(
        """
        clang-format:
          stage: sanity_checks
          image: registry.gitlab.com/libvirt/libvirt-ci/clang-format:master
          needs: []
          script:
            - /clang-format
          artifacts:
            paths:
              - clang-format.patch
            expire_in: 1 week
            when: on_failure
        """)


def _container_job(target, arch, image, allow_failure, optional):
    allow_failure = str(allow_failure).lower()
    jobvars = {
        "NAME": image,
    }
    if optional:
        jobvars["JOB_OPTIONAL"] = "1"

    return textwrap.dedent(
        f"""
        {arch}-{target}-container:
          extends: .container_job
          allow_failure: {allow_failure}
        """) + format_variables(jobvars)


def native_container_job(target, allow_failure, optional):
    return _container_job(target,
                          "x86_64",
                          f"{target}",
                          allow_failure,
                          optional)


def cross_container_job(target, arch, allow_failure, optional):
    return _container_job(target,
                          arch,
                          f"{target}-cross-{arch}",
                          allow_failure,
                          optional)


def format_artifacts(artifacts):
    if artifacts is None:
        return ""

    expire_in = artifacts["expire_in"]
    paths = "\n".join(["      - " + p for p in artifacts["paths"]])

    section = textwrap.indent(textwrap.dedent(f"""
            artifacts:
              expire_in: {expire_in}
              paths:
           """), "  ") + paths + "\n"
    return section[1:]


def merge_vars(system, user):
    for key in user.keys():
        if key in system:
            raise ValueError(
                f"""Attempt to override system variable '{key}' in manifest""")
    return {**user, **system}


def _build_job(target, arch, suffix, variables, template, allow_failure, artifacts):
    allow_failure = str(allow_failure).lower()

    return textwrap.dedent(
        f"""
        {arch}-{target}{suffix}:
          extends: {template}
          needs:
            - job: {arch}-{target}-container
              optional: true
          allow_failure: {allow_failure}
        """) + format_variables(variables) + format_artifacts(artifacts)


def native_build_job(target, suffix, variables, template,
                     allow_failure, optional, artifacts):
    jobvars = merge_vars({
        "NAME": target,
    }, variables)
    if optional:
        jobvars["JOB_OPTIONAL"] = "1"

    return _build_job(target,
                      "x86_64",
                      suffix,
                      jobvars,
                      template,
                      allow_failure,
                      artifacts)


def cross_build_job(target, arch, suffix, variables, template,
                    allow_failure, optional, artifacts):
    jobvars = merge_vars({
        "NAME": target,
        "CROSS": arch
    }, variables)
    if optional:
        jobvars["JOB_OPTIONAL"] = "1"

    return _build_job(target,
                      arch,
                      suffix,
                      jobvars,
                      template,
                      allow_failure,
                      artifacts)


def cirrus_build_job(target, instance_type, image_selector, image_name,
                     pkg_cmd, suffix, variables, allow_failure, optional):
    if pkg_cmd == "brew":
        install_cmd = "brew install"
        upgrade_cmd = "brew upgrade"
        update_cmd = "brew update"
    elif pkg_cmd == "pkg":
        install_cmd = "pkg install -y"
        upgrade_cmd = "pkg upgrade -y"
        update_cmd = "pkg update"
    else:
        raise ValueError(f"Unknown package command {pkg_cmd}")
    allow_failure = str(allow_failure).lower()
    jobvars = merge_vars({
        "NAME": target,
        "CIRRUS_VM_INSTANCE_TYPE": instance_type,
        "CIRRUS_VM_IMAGE_SELECTOR": image_selector,
        "CIRRUS_VM_IMAGE_NAME": image_name,
        "UPDATE_COMMAND": update_cmd,
        "UPGRADE_COMMAND": upgrade_cmd,
        "INSTALL_COMMAND": install_cmd,
    }, variables)
    if optional:
        jobvars["JOB_OPTIONAL"] = "1"

    return textwrap.dedent(
        f"""
        x86_64-{target}{suffix}:
          extends: .cirrus_build_job
          needs: []
          allow_failure: {allow_failure}
        """) + format_variables(jobvars)
