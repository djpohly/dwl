#!/bin/sh

if git tag --contains HEAD | grep -q $1; then
	echo $1
else
	branch="$(git rev-parse --abbrev-ref HEAD)"
	commit="$(git rev-parse --short HEAD)"
	if [ "${branch}" != "main" ]; then
		echo $1-$branch-$commit
	else
		echo $1-$commit
	fi
fi
