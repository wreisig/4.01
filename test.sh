#!/bin/bash
RESPONSE=$(curl -s -i --max-time 15 http://localhost:8080)
HTTP_STATUS=$(echo "$RESPONSE" | head -n 1)

if [[ "$HTTP_STATUS" == *"200 OK"* || "$HTTP_STATUS" == *"502"* ]] && \
    echo "$RESPONSE" | grep -q "bootstrap" && \
    (echo "$RESPONSE" | grep -q "Canvas Due Soon" || echo "$RESPONSE" | grep -q "Canvas error"); then
    echo "Pass"
    exit 0
else
    echo "Fail"
    exit 1
fi