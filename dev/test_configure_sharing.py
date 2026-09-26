"""Local-link configuration checks; no GitHub or payment operations."""
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("sharing", Path(__file__).with_name("configure-sharing.py"))
sharing = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sharing)


class SharingTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        (self.root / "site").mkdir()
        self.config = self.root / "site/config.js"
        self.config.write_text('window.WUWA_SITE = Object.freeze({\n creator: "Fixture",\n repositoryUrl: "", // pending\n supportUrl: "",\n supportLabel: "Optional support"\n});\n')

    def tearDown(self):
        self.temp.cleanup()

    def test_repository_normalizes_and_generates_only_local_links(self):
        result = sharing.configure(self.root, repository="https://github.com/Fixture/vr.git/")
        self.assertEqual(result["repositoryUrl"], "https://github.com/Fixture/vr")
        self.assertFalse(result["published"])
        chooser = (self.root / ".github/ISSUE_TEMPLATE/config.yml").read_text()
        self.assertIn("https://github.com/Fixture/vr/blob/HEAD/docs/TROUBLESHOOTING.md", chooser)
        self.assertIn("custom: []", (self.root / ".github/FUNDING.yml").read_text())
        self.assertEqual(sharing.read_config(self.config)["creator"], "Fixture")

    def test_support_roundtrip_and_clear(self):
        sharing.configure(self.root, support="https://ko-fi.com/fixture", label='Thank you "朋友"')
        self.assertEqual(sharing.read_config(self.config)["supportLabel"], 'Thank you "朋友"')
        self.assertIn('custom: ["https://ko-fi.com/fixture"]', (self.root / ".github/FUNDING.yml").read_text())
        sharing.configure(self.root, repository="https://github.com/Fixture/vr", support="")
        self.assertIn("custom: []", (self.root / ".github/FUNDING.yml").read_text())

    def test_bad_repository_or_support_cannot_change_config(self):
        before = self.config.read_bytes()
        for url in ("http://github.com/a/b", "https://github.com.evil.test/a/b", "https://user:pass@github.com/a/b",
                    "https://github.com/a/b?x=1", "https://github.com/a/b/issues", "https://github.com/a/.."):
            with self.subTest(url=url), self.assertRaises(ValueError):
                sharing.configure(self.root, repository=url)
        with self.assertRaises(ValueError):
            sharing.configure(self.root, support="javascript:alert(1)")
        self.assertEqual(self.config.read_bytes(), before)
        self.assertFalse((self.root / ".github").exists())

    def test_check_is_read_only(self):
        before = self.config.read_bytes()
        result = sharing.configure(self.root, repository="https://github.com/Fixture/vr", check=True)
        self.assertFalse(result["filesChanged"])
        self.assertEqual(self.config.read_bytes(), before)
        self.assertFalse((self.root / ".github").exists())

    def test_handwritten_funding_is_preserved_before_any_write(self):
        folder = self.root / ".github"
        folder.mkdir()
        file = folder / "FUNDING.yml"
        file.write_text("github: existing-author\n")
        before = self.config.read_bytes()
        with self.assertRaises(ValueError):
            sharing.configure(self.root, support="https://ko-fi.com/fixture")
        self.assertEqual(file.read_text(), "github: existing-author\n")
        self.assertEqual(self.config.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
