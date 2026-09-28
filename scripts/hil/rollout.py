"""Prepare additive GitHub configuration for reviewed operator rollout."""
import argparse
import json
import re
from pathlib import Path
import subprocess
from catalog import REPOSITORIES


def gh(path):
    result = subprocess.run(['gh', 'api', path], capture_output=True, text=True, encoding='utf-8')
    if result.returncode:
        raise RuntimeError('GitHub administration access unavailable; restore gh authentication')
    return json.loads(result.stdout)


def prepare(output):
    output.mkdir(parents=True, exist_ok=True)
    pins = json.loads(Path(__file__).with_name('reviewed-workflows.json').read_text())
    if set(pins) != set(REPOSITORIES) or any(not re.fullmatch('[0-9a-f]{40}', sha) for sha in pins.values()):
        raise RuntimeError('Every repository requires an explicitly reviewed immutable workflow SHA')
    repositories = [gh('repos/'+name) for name in REPOSITORIES]
    if any(not r.get('permissions', {}).get('admin') for r in repositories):
        raise RuntimeError('Administration access is required for every selected repository')
    group = dict(name='rad-firmware-validation', visibility='selected',
                 selected_repository_ids=[r['id'] for r in repositories],
                 allows_public_repositories=True, restricted_to_workflows=True,
                 selected_workflows=[name+'/.github/workflows/hardware-run.yml@'+pins[name]
                                     for name in REPOSITORIES])
    (output/'runner-group.json').write_text(json.dumps(group, indent=2)+'\n', encoding='utf-8')
    for name in REPOSITORIES:
        # New additive ruleset: never overwrite existing review/check requirements.
        current = gh('repos/'+name+'/rulesets?includes_parents=true')
        short = name.split('/')[1]
        (output/(short+'-existing-rulesets.json')).write_text(json.dumps(current, indent=2)+'\n', encoding='utf-8')
        ruleset = dict(name='PR firmware and hardware validation', target='branch', enforcement='disabled',
                       conditions={'ref_name': {'include': ['refs/heads/main','refs/heads/staging'], 'exclude': []}},
                       bypass_actors=[], rules=[dict(type='required_status_checks', parameters={
                           'strict_required_status_checks_policy': True,
                           'required_status_checks': [dict(context=context, integration_id=15368)
                                                      for context in ('Build validation','Hardware validation')]})])
        (output/(short+'-ruleset.json')).write_text(json.dumps(ruleset, indent=2)+'\n', encoding='utf-8')
    print('Prepared disabled, additive rulesets and restricted runner-group payload. Review before applying.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('command', choices=['prepare'])
    parser.add_argument('--output', type=Path, required=True)
    prepare(parser.parse_args().output)
