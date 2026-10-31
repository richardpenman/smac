#!/bin/bash
# The version of a release workflow run (.github/workflows/release.yml): the tag's name on a v*
# tag, build-N-SHA on a push to main (N the run number, so the builds sort), else dev-SHA.
if [[ $GITHUB_REF == refs/tags/v* ]]; then echo "$GITHUB_REF_NAME"
elif [[ $GITHUB_EVENT_NAME == push && $GITHUB_REF == refs/heads/main ]]; then echo "build-$GITHUB_RUN_NUMBER-${GITHUB_SHA::7}"
else echo "dev-${GITHUB_SHA::7}"
fi
