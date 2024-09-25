#!/usr/bin/env bash

set -exuo pipefail

FILE_PATTERN="{!(dist|node_modules)/**/*.{js,jsx,ts,tsx,json},*.{js,jsx,ts,tsx,json}}"

i18next "${FILE_PATTERN}" [-oc] -c "./i18next-parser.config.js" -o "locales/\$LOCALE/\$NAMESPACE.json"
