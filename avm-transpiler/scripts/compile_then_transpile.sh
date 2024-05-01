#!/usr/bin/env bash
set -eu

NARGO=${NARGO:-nargo}
TRANSPILER=${TRANSPILER:-avm-transpiler}

# Create a temporary file to capture and parse nargo's stdout while still printing it to the console.
# To avoid a situation where the script fails and the temporary file isn't removed,
# create file descriptors for writing/reading the temporary file and then remove the file.
tmpfile=$(mktemp)
exec 3>"$tmpfile"
exec 4<"$tmpfile"
rm "$tmpfile"

# Forward all arguments to nargo, tee output to the tmp file
$NARGO $@ | tee $(cat >&3)

# Parse nargo's output (captured in the tmp file) to determine which artifacts to transpile
artifacts_to_transpile=$(grep -oP 'Saved contract artifact to: \K.*' <&4)

# Transpile each artifact
for artifact in "$artifacts_to_transpile"; do
	$TRANSPILER "$artifact"
done
