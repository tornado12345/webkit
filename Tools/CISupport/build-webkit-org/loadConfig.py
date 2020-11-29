# Copyright (C) 2017-2020 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1.  Redistributions of source code must retain the above copyright
#     notice, this list of conditions and the following disclaimer.
# 2.  Redistributions in binary form must reproduce the above copyright
#     notice, this list of conditions and the following disclaimer in the
#     documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS'' AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import os
USE_BUILDBOT_VERSION2 = os.getenv('USE_BUILDBOT_VERSION2') is not None

if USE_BUILDBOT_VERSION2:
    from buildbot.worker import Worker
else:
    from buildbot.buildslave import BuildSlave
from buildbot.scheduler import AnyBranchScheduler, Triggerable, Nightly
from buildbot.schedulers.forcesched import FixedParameter, ForceScheduler, StringParameter, BooleanParameter
from buildbot.schedulers.filter import ChangeFilter
from buildbot.process import buildstep, factory, properties

from factories import *

import re
import json
import operator

from committer_auth import CommitterAuth
import make_passwords_json
import wkbuild

trunk_filter = ChangeFilter(branch=["trunk", None])
buildbot_identifiers_re = re.compile('^[a-zA-Z_-][a-zA-Z0-9_-]*$')

BUILDER_NAME_LENGTH_LIMIT = 70
STEP_NAME_LENGTH_LIMIT = 50


def pickLatestBuild(builder, requests):
    return max(requests, key=operator.attrgetter("submittedAt"))


def loadBuilderConfig(c, is_test_mode_enabled=False):
    # FIXME: These file handles are leaked.
    if is_test_mode_enabled:
        passwords = {}
    else:
        passwords = json.load(open('passwords.json'))
    results_server_api_key = passwords.get('results-server-api-key')
    if results_server_api_key:
        os.environ['RESULTS_SERVER_API_KEY'] = results_server_api_key

    config = json.load(open('config.json'))
    if USE_BUILDBOT_VERSION2:
        c['workers'] = [Worker(worker['name'], passwords.get(worker['name'], 'password'), max_builds=1) for worker in config['workers']]
    else:
        c['slaves'] = [BuildSlave(worker['name'], passwords.get(worker['name'], 'password'), max_builds=1) for worker in config['workers']]

    c['schedulers'] = []
    for scheduler in config['schedulers']:
        if "change_filter" in scheduler:
            scheduler["change_filter"] = globals()[scheduler["change_filter"]]
        schedulerType = globals()[scheduler.pop('type')]
        # Python 2.6 can't handle unicode keys as keyword arguments:
        # http://bugs.python.org/issue2646.  Modern versions of json return
        # unicode strings from json.load, so we map all keys to str objects.
        scheduler = dict(map(lambda key_value_pair: (str(key_value_pair[0]), key_value_pair[1]), scheduler.items()))

        c['schedulers'].append(schedulerType(**scheduler))

    forceScheduler = ForceScheduler(
        name="force",
        builderNames=[str(builder['name']) for builder in config['builders']],
        reason=StringParameter(name="reason", default="", size=40),

        # Validate SVN revision: number or empty string
        revision=StringParameter(name="revision", default="", regex=re.compile(r'^(\d*)$')),

        # Disable default enabled input fields: branch, repository, project, additional properties
        branch=FixedParameter(name="branch"),
        repository=FixedParameter(name="repository"),
        project=FixedParameter(name="project"),
        properties=[BooleanParameter(name="is_clean", label="Force Clean build")]
    )
    c['schedulers'].append(forceScheduler)

    c['builders'] = []
    for builder in config['builders']:
        for workerName in builder['workernames']:
            for worker in config['workers']:
                if worker['name'] != workerName or worker['platform'] == '*':
                    continue

                if worker['platform'] != builder['platform']:
                    raise Exception('Builder {} is for platform {} but has worker {} for platform {}!'.format(builder['name'], builder['platform'], worker['name'], worker['platform']))
                break

        if not USE_BUILDBOT_VERSION2:
            builder['slavenames'] = builder.pop('workernames')
        platform = builder['platform']

        factoryName = builder.pop('factory')
        factory = globals()[factoryName]
        factorykwargs = {}
        for key in "platform", "configuration", "architectures", "triggers", "additionalArguments", "device_model":
            value = builder.pop(key, None)
            if value:
                factorykwargs[key] = value

        builder["factory"] = factory(**factorykwargs)

        builder_name = builder['name']
        if len(builder_name) > BUILDER_NAME_LENGTH_LIMIT:
            raise Exception('Builder name "{}" is longer than maximum allowed by Buildbot ({} characters).'.format(builder_name, BUILDER_NAME_LENGTH_LIMIT))
        if not buildbot_identifiers_re.match(builder_name):
            raise Exception('Builder name "{}" is not a valid buildbot identifier.'.format(builder_name))
        for step in builder["factory"].steps:
            if USE_BUILDBOT_VERSION2:
                step_name = step.buildStep().name
            else:
                step_name = step[0].name
            if len(step_name) > STEP_NAME_LENGTH_LIMIT:
                raise Exception('step name "{}" is longer than maximum allowed by Buildbot ({} characters).'.format(step_name, STEP_NAME_LENGTH_LIMIT))
            if not buildbot_identifiers_re.match(step_name):
                raise Exception('step name "{}" is not a valid buildbot identifier.'.format(step_name))

        if platform.startswith('mac'):
            category = 'AppleMac'
        elif platform.startswith('ios'):
            category = 'iOS'
        elif platform == 'win':
            category = 'AppleWin'
        elif platform.startswith('gtk'):
            category = 'GTK'
        elif platform.startswith('wpe'):
            category = 'WPE'
        elif platform == 'wincairo':
            category = 'WinCairo'
        elif platform.startswith('playstation'):
            category = 'PlayStation'
        else:
            category = 'misc'

        if (category in ('AppleMac', 'AppleWin', 'iOS')) and factoryName != 'BuildFactory':
            builder['nextBuild'] = pickLatestBuild

        if USE_BUILDBOT_VERSION2:
            builder['tags'] = getTagsForBuilder(builder)
        else:
            builder['category'] = category
        c['builders'].append(builder)


class PlatformSpecificScheduler(AnyBranchScheduler):
    def __init__(self, platform, branch, **kwargs):
        self.platform = platform
        filter = ChangeFilter(branch=[branch, None], filter_fn=self.filter)
        AnyBranchScheduler.__init__(self, name=platform, change_filter=filter, **kwargs)

    def filter(self, change):
        return wkbuild.should_build(self.platform, change.files)


def getInvalidTags():
    """
    We maintain a list of words which we do not want to display as tag in buildbot.
    We generate a list of tags by splitting the builder name. We do not want certain words as tag.
    For e.g. we don't want '11'as tag for builder iOS-11-Simulator-EWS
    """
    invalid_tags = [str(i) for i in range(0, 20)]
    invalid_tags.extend(['EWS', 'TryBot'])
    return invalid_tags


def getValidTags(tags):
    return list(set(tags) - set(getInvalidTags()))


def getTagsForBuilder(builder):
    keywords = filter(None, re.split('[, \-_:()]+', str(builder['name'])))
    return getValidTags(keywords)
