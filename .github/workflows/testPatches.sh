#!/bin/bash

update_line() {
    local line="$1"         # line from opened file is passed in
    local testResult="$2"   # result from git diff

    echo "Processing line: $line with additional argument: $testResult"
}

dwlSrcDirectory="$1"
wikiDirectory="$2"
patchRE="https://github\.com/[^/]+/[^/]+/compare/[^/]+\.patch"

for file in "$wikiDirectory"/*.md; do
    if [ -f "$file" ]; then
        if ! grep -q "Download" "$file"; then
            echo "no download heading found in $file, skipping..."
            continue
        fi

        while IFS= read -r line
        do
            if [[ $line =~ $patchRE ]]; then
                extractedURL=${BASH_REMATCH[0]}
                response=$(curl -s -w "%{http_code}" -o - "$extractedURL")

                http_status_code="${response: -3}"
                patchContent="${response:0:-3}"

                if [ "$http_status_code" -ne 200 ] || [ -z "$patchContent" ]; then
                    echo "⚠️ - $extractedURL"
                    update_line "$line" "inaccessible"
                    continue
                fi

                git -C "$dwlSrcDirectory" apply --check <<< "$patchContent" > /dev/null 2>&1
                patchApplicationExitCode=$?

                if [ $patchApplicationExitCode -eq 0 ]; then
                    echo "✅ - $extractedURL"
                    update_line "$line" "pass"
                else
                    echo "❌ - $extractedURL"
                    update_line "$line" "fail"
                fi
            fi
        done < "$file"
    fi
done
