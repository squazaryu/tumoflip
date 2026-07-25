# -*- coding: utf-8 -*-

import os
import logging
from typing import Dict

from repair_profile_ui import repair_profile_ui

# Set up logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

def main() -> None:
    """
    The main entry point of the application.
    """
    # Repair the profile source
    repair_profile_ui()

if __name__ == "__main__":
    main()