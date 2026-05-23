#!/usr/bin/env bash
/usr/bin/pkg-config "$@" | sed 's/-mfpmath=sse//g; s/-msse2//g; s/-msse//g'
