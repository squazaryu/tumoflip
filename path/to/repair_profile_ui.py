# -*- coding: utf-8 -*-

import os
import logging
from typing import Dict

from repair_profile import repair_profile

# Set up logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

def repair_profile_ui() -> None:
    """
    Repair a missing profile source using a graphical user interface.
    """
    # Get the profile path from the user
    profile_path = input("Enter the path to the profile file: ")

    # Get the source path from the user
    source_path = input("Enter the path to the source file: ")

    # Repair the profile source
    if repair_profile(profile_path, source_path):
        logger.info("Profile source repaired successfully.")
    else:
        logger.error("Failed to repair profile source.")