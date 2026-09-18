#!/bin/bash
RESPONSE=$(curl -s -i http://localhost:8080)
HTTP_STATUS=$(echo "$RESPONSE" | head -n 1)

if [[ "$HTTP_STATUS" == *"200 OK"* ]] && echo "$RESPONSE" | grep -q "Hello World" && echo "$RESPONSE" | grep -q "bootstrap"; then
    echo "Pass"
    exit 0
else
    echo "Fail"
    exit 1
fi