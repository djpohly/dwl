#!/bin/bash

update_line() {
    local line="$1"         # line from opened file is passed in
    local testResult="$2"   # result from git diff

    echo "Processing line: $line with additional argument: $additional_argument"
}

wikiDirectory=./dwl.wiki
dwlSrcDirectory=./dwl
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
                patchAccessResult=$(curl -s -w "%{http_code}" -o /dev/null "$extractedURL")
                if [ "$patchAccessResult" -ne 200 ]; then
                    update_line "$line" "inaccessible"
                    continue
                fi

                git -C $dwlSrcDirectory apply --check <(curl -s "$extractedURL") > /dev/null 2>&1
                patchApplicationExitCode=$?
                if [ $patchApplicationExitCode -eq 0 ]; then
                    printf "\e[32m[PASS]\e[0m %-40s\n" "$extractedURL"
                    update_line "$line" "pass"
                else
                    printf "\e[31m[FAIL]\e[0m %-40s\n" "$extractedURL"
                    update_line "$line" "fail"
                fi

            fi
        done < "$file"


    fi
done
