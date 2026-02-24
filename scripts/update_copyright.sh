#!/bin/bash
# Copyright 2025 Mitchell. See LICENSE.
# Updates the copyright year of repository files.

if [ "$(uname)" == "Darwin" ]; then
	sed () {
		gsed "$@"
	}
fi

new_year=2025
prev_year=$(( $new_year - 1 ))
repo=$(git rev-parse --show-toplevel)

git ls-files $repo | xargs sed -i '' -e "s/\-$prev_year M/-$new_year M/;"
git ls-files $repo | xargs sed -i '' -e "s/ $prev_year M/ $prev_year-$new_year M/;"
