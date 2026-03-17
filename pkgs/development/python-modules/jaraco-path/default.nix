{
  lib,
  buildPythonPackage,
  fetchFromGitHub,
  setuptools-scm,
  pyobjc-framework-Cocoa,
  pytestCheckHook,
  stdenv,
}:

buildPythonPackage rec {
  pname = "jaraco-path";
  version = "3.7.2";
  pyproject = true;

  src = fetchFromGitHub {
    owner = "jaraco";
    repo = "jaraco.path";
    tag = "v${version}";
    hash = "sha256-uLkNMhB7aeDJ3fF0Ynjd8MD6+CTKKH8vsB5cH9RPcok=";
  };

  build-system = [ setuptools-scm ];

  dependencies = lib.optionals stdenv.hostPlatform.isDarwin [
    pyobjc-framework-Cocoa
  ];

  # The wheel declares "pyobjc" (meta-package) as a dependency, but nixpkgs
  # only packages individual pyobjc frameworks. jaraco.path only uses
  # Foundation (from pyobjc-framework-Cocoa), so we remove the meta-package
  # dep and provide the specific framework instead.
  pythonRemoveDeps = lib.optionals stdenv.hostPlatform.isDarwin [ "pyobjc" ];

  pythonImportsCheck = [ "jaraco.path" ];

  nativeCheckInputs = [ pytestCheckHook ];

  disabledTests = lib.optionals stdenv.hostPlatform.isDarwin [
    # Expects ~/Library to exist, which it doesn't in the sandbox
    "test_is_hidden_Darwin"
  ];

  meta = {
    changelog = "https://github.com/jaraco/jaraco.path/blob/${src.tag}/NEWS.rst";
    description = "Miscellaneous path functions";
    homepage = "https://github.com/jaraco/jaraco.path";
    license = lib.licenses.mit;
    maintainers = with lib.maintainers; [ dotlambda ];
  };
}
