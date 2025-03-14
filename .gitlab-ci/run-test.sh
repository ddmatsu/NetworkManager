#!/bin/bash

set -ex

die() {
    printf "%s\n" "$*" >&2
    exit 1
}

export PAGER=cat
export OMP_NUM_THREADS=1

IS_FEDORA=0
IS_CENTOS=0
IS_ALPINE=0
grep -q '^NAME=.*\(CentOS\)' /etc/os-release && IS_CENTOS=1
grep -q '^NAME=.*\(Fedora\)' /etc/os-release && IS_FEDORA=1
grep -q '^NAME=.*\(Alpine\)' /etc/os-release && IS_ALPINE=1

IS_CENTOS_7=0
if [ $IS_CENTOS = 1 ]; then
    if grep -q '^VERSION_ID=.*\<7\>' /etc/os-release ; then
        IS_CENTOS_7=1
    fi
fi

do_clean() {
    git clean -fdx

    git status
    git diff --exit-code
}

ARTIFACT_DIR=/tmp/nm-artifact
rm -rf "$ARTIFACT_DIR"
mkdir -p "$ARTIFACT_DIR"

uname -a
! command -v locale &>/dev/null || locale -a
meson --version

! command -v dpkg &>/dev/null || dpkg -l
! command -v dnf  &>/dev/null || dnf list --installed
! command -v apk  &>/dev/null || apk -v info

# We have a unit test that check that `ci-fairy generate-template`
# is equal to our .gitlab-ci.yml file. However, on gitlab-ci we
# also have a dedicate test for the same thing. We don't need
# to run that test as part of the build. Disable it.
export NMTST_SKIP_CHECK_GITLAB_CI=1

# Assert that "$1" is one of the valid values for NM_TEST_SELECT_RUN. die() otherwise.
is_run_selected_assert() {
    { set +x; } 2>/dev/null
    local run="$1"
    local a

    # These are the supported $NM_TEST_SELECT_RUN values.
    local _CHECK_RUN_LIST=(
        meson+gcc+docs+valgrind
        meson+clang
        rpm+meson
        tarball+meson
        tarball
        subtree

        all
        none
    )

    if [ "$run" = all ] ; then
        set -x
        return 0
    fi

    for a in "${_CHECK_RUN_LIST[@]}" ; do
        if [ "$a" = "$run" ] ; then
            set -x
            return 0
        fi
    done
    die "invalid NM_TEST_SELECT_RUN value \"$1\""
}

[ -z "$NM_TEST_SELECT_RUN" ] && NM_TEST_SELECT_RUN=all
is_run_selected_assert "$NM_TEST_SELECT_RUN"

is_run_selected() {
    local run="$1"

    is_run_selected_assert "$run"

    # Usually, we run the build several times. However, for testing
    # the build script manually, it can be useful to explicitly select
    # one step to run. For example, if step 3 is known to fail, you
    # can still manually run step A by setting NM_TEST_SELECT_RUN=A.

    test "$NM_TEST_SELECT_RUN" = all -o "$NM_TEST_SELECT_RUN" = "$run"
}

die_with_testlog() {
    mv ./build/meson-logs/testlog.txt ./testlog.txt
    exit 1
}

if is_run_selected meson+gcc+docs+valgrind ; then
    do_clean
    CC=gcc WITH_DOCS=1 WITH_VALGRIND=1 contrib/scripts/nm-ci-run.sh || die_with_testlog
    mv INST/share/gtk-doc/html "$ARTIFACT_DIR/docs-html"
fi

if is_run_selected meson+clang; then
    do_clean
    CC=clang WITH_DOCS=0 contrib/scripts/nm-ci-run.sh || die_with_testlog
fi

if is_run_selected rpm+meson; then
    do_clean
    if [[ $IS_FEDORA = 1 ]]; then
        ./contrib/fedora/rpm/build_clean.sh -g -w crypto_gnutls -w debug -w iwd -w test -w meson || die_with_testlog
    fi
fi

if is_run_selected tarball && [ "$NM_BUILD_TARBALL" = 1 ]; then
    do_clean
    SIGN_SOURCE=0 ./contrib/fedora/rpm/build_clean.sh -r || die_with_testlog
    mv ./build/meson-dist/NetworkManager-1*.tar.xz "$ARTIFACT_DIR/"
    mv ./contrib/fedora/rpm/latest/SRPMS/NetworkManager-1*.src.rpm "$ARTIFACT_DIR/"
    do_clean
fi

if is_run_selected tarball+meson; then
    do_clean
    CC=gcc WITH_DOCS=1 CONFIGURE_ONLY=1 contrib/scripts/nm-ci-run.sh || die_with_testlog
fi

###############################################################################

test_subtree() {
    local d="$1"
    local cc="$2"

    if meson --version | grep -q '^0\.[0-5][0-9]\.' ; then
        # These subprojects require a newer meson than NetworkManager. Skip the test.
        return 0
    fi

    do_clean
    pushd ./src/$d

    ARGS=()
    if [ "$d" = n-acd ]; then
        ARGS+=('-Debpf=false')
    fi

    CC="$cc" CFLAGS="-Werror -Wall" meson build "${ARGS[@]}"
    ninja -v -C build test

    popd
}

if is_run_selected subtree; then
    do_clean
    for d in c-list c-rbtree c-siphash c-stdaux n-acd n-dhcp4 ; do
        for cc in gcc clang; do
            test_subtree "$d" "$cc"
        done
    done
fi

###############################################################################

if [ "$NM_BUILD_TARBALL" = 1 ]; then
    do_clean
    if is_run_selected meson+gcc+docs+valgrind ; then
        mv "$ARTIFACT_DIR/docs-html/" ./
    fi
    if is_run_selected tarball ; then
        mv \
           "$ARTIFACT_DIR"/NetworkManager-1*.tar.xz \
           "$ARTIFACT_DIR"/NetworkManager-1*.src.rpm \
           ./
    fi
fi

echo "BUILD SUCCESSFUL!!"
