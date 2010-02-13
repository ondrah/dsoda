#!/bin/sh

build_bin() {
	fakeroot dpkg-buildpackage -b
}

build_src() {
	fakeroot dpkg-buildpackage -S -Idebian -I.git -Imkpkg.sh
}

case "$1" in
	b)
		build_bin
	;;
	s)
		build_src
	;;
	all)
		build_bin
		build_src
	;;
	*)
		echo "usage: $0 b|s|all"
	;;
esac
