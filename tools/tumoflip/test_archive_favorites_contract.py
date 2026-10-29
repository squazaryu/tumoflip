"""A missing lazy writer must stop updater publication, never erase a list."""
import hashlib
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from tools.tumoflip import validate_release as release


class ArchiveFavoritesResourceTests(unittest.TestCase):
    def test_missing_or_changed_writer_is_rejected_in_native_resources(self):
        with tempfile.TemporaryDirectory(prefix='favorites-resource-') as directory:
            root = Path(directory)
            hashes = {}
            for name in ('subghz_frequency_analyzer', 'subghz_add_manually', 'subghz_workspaces'):
                relative = f'apps_data/subghz/plugins/{name}.fal'
                path = root / relative; path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'fixture')
                hashes[relative] = hashlib.sha256(b'fixture').hexdigest()
            relative = 'apps_data/archive/plugins/archive_favorites.fal'
            with patch.object(release, 'resources_archive_hashes', return_value=hashes):
                with self.assertRaises(release.ValidationError):
                    release.validate_subghz_feature_resources(root, root, root / 'archive')
                path = root / relative; path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'writer')
                with self.assertRaises(release.ValidationError):
                    release.validate_subghz_feature_resources(root, root, root / 'archive')
                hashes[relative] = hashlib.sha256(b'writer').hexdigest()
                release.validate_subghz_feature_resources(root, root, root / 'archive')


if __name__ == '__main__': unittest.main()
