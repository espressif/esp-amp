#!/bin/bash
set -u
set -o pipefail

# --- Script Arguments ---
if [ "$#" -ne 3 ]; then
  echo "Usage: $0 <TARGET> <IDF_VER> <BUILD_DIR>"
  echo "Example: $0 esp32c6 release-v5.4 build_idf-release-v5.4_esp32c6"
  exit 1
fi

TARGET="$1"
IDF_VER="$2"
BUILD_DIR="$3"

echo "--- Test Script Started for Target: ${TARGET}, IDF: ${IDF_VER}, Build Dir: ${BUILD_DIR} ---"

echo "Running pytest for ${TARGET}..."
export ESPBAUD=115200

MAX_ATTEMPTS=4
PYTEST_COMMAND="uv run pytest --target ${TARGET} --junit-xml=result.xml --build-dir=${BUILD_DIR}"
XML_REPORT_FILE="result.xml"

for attempt in $(seq 1 $MAX_ATTEMPTS); do
  echo "Pytest attempt ${attempt}/${MAX_ATTEMPTS} for target ${TARGET}, IDF ${IDF_VER}"

  if [ "${attempt}" -eq 1 ]; then
    INITIAL_DELAY=$(((RANDOM % 15) + 1))
    echo "Initial random delay of ${INITIAL_DELAY} seconds before first pytest run..."
    sleep ${INITIAL_DELAY}
  fi

  echo "Ensuring no previous XML report exists: ${XML_REPORT_FILE}"
  rm -f "${XML_REPORT_FILE}"

  echo "Executing: ${PYTEST_COMMAND}"
  set +e
  bash -c "${PYTEST_COMMAND}"
  pytest_exit_code=$?
  set -e

  if [ ${pytest_exit_code} -ne 0 ]; then
    echo "Pytest command finished with a non‐zero exit code: ${pytest_exit_code} on attempt ${attempt}."
  else
    echo "Pytest command finished with exit code 0 on attempt ${attempt}."
  fi

  actual_test_count=0
  test_count_sufficient=false
  xml_parsable_and_exists=false

  if [ -f "${XML_REPORT_FILE}" ] && [ -r "${XML_REPORT_FILE}" ]; then
    xml_parsable_and_exists=true
    count_output=$(grep -c "<testcase" "${XML_REPORT_FILE}" 2>/dev/null || echo "0")

    if [[ "$count_output" =~ ^[0-9]+$ ]]; then
      actual_test_count=$count_output
    else
      echo "Warning: Could not parse test case count from ${XML_REPORT_FILE} (output: '${count_output}'). Assuming 0."
      actual_test_count=0
    fi
    echo "Found ${actual_test_count} test cases in ${XML_REPORT_FILE}."

    if [ "${actual_test_count}" -gt "1" ]; then
      test_count_sufficient=true
    fi
  else
    echo "XML report ${XML_REPORT_FILE} not found or not readable after pytest execution."
  fi

  if [ "${pytest_exit_code}" -eq 0 ] && [ "${test_count_sufficient}" = true ]; then
    echo "Pytest successful on attempt ${attempt} with ${actual_test_count} test cases."
    echo "--- Test Script Finished Successfully ---"
    exit 0
  else
    failure_reason=""
    if [ "${pytest_exit_code}" -ne 0 ]; then
      failure_reason="Pytest failed with exit code ${pytest_exit_code}"
    fi
    if [ "${test_count_sufficient}" = false ]; then
      if [ -n "${failure_reason}" ]; then failure_reason+=" and "; fi
      if [ "${xml_parsable_and_exists}" = true ]; then
        failure_reason+="insufficient test cases found (${actual_test_count}), which implies that firmware flashing is probably failed."
      else
        failure_reason+="XML report missing or unreadable."
      fi
    fi

    echo "Pytest attempt ${attempt} failed: ${failure_reason}."

    if [ "${attempt}" -lt "${MAX_ATTEMPTS}" ] && [ "${test_count_sufficient}" = false ]; then
      BACKOFF_SECONDS=$(((RANDOM % 15) * (attempt) + 15)) # Increase backoff with each attempt
      echo "Retrying after a backoff of ${BACKOFF_SECONDS} seconds..."
      sleep ${BACKOFF_SECONDS}
    else
      echo "Pytest permanently failed for ${TARGET}, IDF ${IDF_VER}."
      echo "Final failure reason(s): ${failure_reason}."
      echo "--- Test Script Finished with Failure ---"
      exit 1
    fi
  fi
done

echo "Error: Pytest loop completed without explicit success or failure. Marking as failed."
echo "--- Test Script Finished with Failure (Fallback) ---"
exit 1
