# SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: CC0-1.0

import pytest
from pytest_embedded import Dut


@pytest.mark.target("esp32c6")
@pytest.mark.target("esp32c5")
@pytest.mark.env("generic")
def test_lp_core(dut: Dut) -> None:
    dut.run_all_single_board_cases(reset=True, timeout=60)
