# -*- coding: utf-8 -*-

import os
import json
import logging

from typing import Dict, List

# Set up logging
logging.basicConfig(level=logging.INFO)
logger = logging.getLogger(__name__)

def repair_profile(profile_path: str, source_path: str) -> bool:
    """
    Repair a missing profile source.

    Args:
        profile_path (str): The path to the profile file.
        source_path (str): The path to the source file.

    Returns:
        bool: True if the repair was successful, False otherwise.
    """
    try:
        # Load the profile data
        with open(profile_path, 'r') as f:
            profile_data = json.load(f)

        # Find the missing source
        missing_source = None
        for source in profile_data['sources']:
            if source['path'] != source_path:
                missing_source = source
                break

        if missing_source is None:
            logger.info("No missing source found.")
            return False

        # Update the source path
        missing_source['path'] = source_path

        # Save the updated profile data
        with open(profile_path, 'w') as f:
            json.dump(profile_data, f, indent=4)

        logger.info("Profile source repaired successfully.")
        return True

    except json.JSONDecodeError as e:
        logger.error(f"Failed to parse profile data: {e}")
        return False

    except Exception as e:
        logger.error(f"An error occurred: {e}")
        return False