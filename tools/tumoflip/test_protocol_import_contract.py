"""Protocol FALs use the public resolver, unlike composite feature FALs."""
import contextlib
import io
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.tumoflip import validate_subghz_feature_imports as validator


class ProtocolImportContractTests(unittest.TestCase):
    def check(self, imported):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            api = root / "targets/f7/api_symbols.csv"
            api.parent.mkdir(parents=True)
            api.write_text("entry,status,name,type,params\nFunction,+,public_method,void,void\n")
            host = root / "applications/main/subghz/api/subghz_app_api_table_i.h"
            host.parent.mkdir(parents=True)
            host.write_text("API_METHOD(private_method, void, void)\n")
            build = root / "build"
            (build / ".extapps").mkdir(parents=True)
            (build / ".extapps/protocol_example.fal").write_bytes(b"fixture")

            def nm(args, **kwargs):
                if "--defined-only" in args:
                    return "0000 T private_method\n"
                return " U " + (imported if args[-1].endswith("protocol_example.fal") else "public_method") + "\n"

            with patch.object(validator, "ROOT", root), \
                 patch.object(validator, "PROTOCOL_PACKS", {"protocol_example.fal"}, create=True), \
                 patch.object(validator.subprocess, "check_output", side_effect=nm), \
                 contextlib.redirect_stdout(io.StringIO()):
                validator.validate(build, "nm")

    def test_private_host_symbol_is_not_a_protocol_fal_export(self):
        with self.assertRaisesRegex(ValueError, "protocol_example"):
            self.check("private_method")

    def test_public_method_is_a_valid_protocol_fal_import(self):
        self.check("public_method")


if __name__ == "__main__":
    unittest.main()
