#!/usr/bin/env ruby
#
# Copyright (c) 2017-2020 Apple Inc. All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
# 1. Redistributions of source code must retain the above copyright
#    notice, this list of conditions and the following disclaimer.
# 2. Redistributions in binary form must reproduce the above copyright
#    notice, this list of conditions and the following disclaimer in the
#    documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY APPLE INC. AND ITS CONTRIBUTORS ``AS IS''
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
# PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL APPLE INC. OR ITS CONTRIBUTORS
# BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
# CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
# SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
# ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
# THE POSSIBILITY OF SUCH DAMAGE.

require "fileutils"
require 'erb'
require 'optparse'
require 'yaml'

options = { 
  :additionalSettings => nil,
  :basePreferences => nil,
  :debugPreferences => nil,
  :experimentalPreferences => nil,
  :internalPreferences => nil,
  :outputDirectory => nil
}
optparse = OptionParser.new do |opts|
    opts.banner = "Usage: #{File.basename($0)} --input file"

    opts.separator ""

    opts.on("--additionalSettings input", "file to generate settings from") { |additionalSettings| options[:additionalSettings] = additionalSettings }
    opts.on("--base input", "file to generate settings from") { |basePreferences| options[:basePreferences] = basePreferences }
    opts.on("--debug input", "file to generate debug settings from") { |debugPreferences| options[:debugPreferences] = debugPreferences }
    opts.on("--experimental input", "file to generate experimental settings from") { |experimentalPreferences| options[:experimentalPreferences] = experimentalPreferences }
    opts.on("--internal input", "file to generate internal settings from") { |internalPreferences| options[:internalPreferences] = internalPreferences }
    opts.on("--outputDir output", "directory to generate file in") { |output| options[:outputDirectory] = output }
end

optparse.parse!

if !options[:additionalSettings] || !options[:basePreferences] || !options[:debugPreferences] || !options[:experimentalPreferences] || !options[:internalPreferences]
  puts optparse
  exit -1
end

if !options[:outputDirectory]
  options[:outputDirectory] = Dir.getwd
end

FileUtils.mkdir_p(options[:outputDirectory])

parsedSettings = begin
  YAML.load_file(options[:additionalSettings])
rescue ArgumentError => e
  puts "Could not parse input file: #{e.message}"
  exit(-1)
end

def load(path)
  parsed = begin
    YAML.load_file(path)
  rescue ArgumentError => e
    puts "ERROR: Could not parse input file: #{e.message}"
    exit(-1)
  end

  previousName = nil
  parsed.keys.each do |name|
    if previousName != nil and previousName > name
      puts "ERROR: Input file #{path} is not sorted. First out of order name found is '#{name}'."
      exit(-1)
    end
    previousName = name
  end

  parsed
end

parsedPreferences = {}
parsedPreferences.merge!(load(options[:basePreferences]))
parsedPreferences.merge!(load(options[:debugPreferences]))
parsedPreferences.merge!(load(options[:experimentalPreferences]))
parsedPreferences.merge!(load(options[:internalPreferences]))


class SettingFromWebPreferences
  attr_accessor :name
  attr_accessor :options
  attr_accessor :type
  attr_accessor :initial
  attr_accessor :excludeFromInternalSettings
  attr_accessor :conditional
  attr_accessor :onChange
  attr_accessor :getter
  attr_accessor :inspectorOverride
  attr_accessor :customImplementation

  def initialize(name, options)
    @name = normalizeNameForWebCore(name, options)
    @options = options
    @type = options["type"]
    @initial = options["defaultValue"]["WebCore"]["default"]
    @excludeFromInternalSettings = options["webcoreExcludeFromInternalSettings"] || false
    @conditional = options["condition"]
    @onChange = options["webcoreOnChange"]
    @getter = options["webcoreGetter"]
    @inspectorOverride = options["inspectorOverride"]
    @customImplementation = options["webcoreImplementation"] == "custom"
  end

  def normalizeNameForWebCore(name, options)
    if options["webcoreName"]
      options["webcoreName"]
    elsif name.start_with?("VP")
      name[0..1].downcase + name[2..name.length]
    elsif name.start_with?("CSSOM", "HTTPS")
      name
    elsif name.start_with?("CSS", "XSS", "FTP", "DOM", "DNS", "PDF", "ICE", "HDR")
      name[0..2].downcase + name[3..name.length]
    elsif name.start_with?("HTTP")
      name[0..3].downcase + name[4..name.length]
    else
      name[0].downcase + name[1..name.length]
    end
  end

  def valueType?
    @type != "String" && @type != "URL"
  end

  def idlType
    # FIXME: Add support for more types including enum types.
    if @type == "uint32_t"
      "unsigned long"
    elsif @type == "double"
      "double"
    elsif @type == "String"
      "DOMString"
    elsif @type == "bool"
      "boolean"
    else
      nil
    end
  end

  def parameterType
    if valueType?
      @type
    else
      "const #{@type}&"
    end
  end

  def hasComplexSetter?
    @onChange != nil
  end

  def hasComplexGetter?
    hasInspectorOverride?
  end

  def setterFunctionName
    if @name.start_with?("css", "xss", "ftp", "dom", "dns", "ice", "hdr")
      "set" + @name[0..2].upcase + @name[3..@name.length]
    else
      "set" + @name[0].upcase + @name[1..@name.length]
    end
  end

  def getterFunctionName
    @getter || @name
  end

  def hasInspectorOverride?
    @inspectorOverride == true
  end
end


class SettingFromWebCore
  attr_accessor :name
  attr_accessor :options
  attr_accessor :type
  attr_accessor :initial
  attr_accessor :excludeFromInternalSettings
  attr_accessor :conditional
  attr_accessor :onChange
  attr_accessor :getter
  attr_accessor :inspectorOverride
  attr_accessor :customImplementation
  
  def initialize(name, options)
    @name = name
    @options = options
    @type = options["type"] || "bool"
    @initial = options["initial"]
    @excludeFromInternalSettings = options["excludeFromInternalSettings"] || false
    @conditional = options["conditional"]
    @onChange = options["onChange"]
    @getter = options["getter"]
    @inspectorOverride = options["inspectorOverride"]
  end

  def valueType?
    @type != "String" && @type != "URL"
  end

  def idlType
    # FIXME: Add support for more types including enum types.
    if @type == "uint32_t"
      "unsigned long"
    elsif @type == "double"
      "double"
    elsif @type == "String"
      "DOMString"
    elsif @type == "bool"
      "boolean"
    else
      nil
    end
  end

  def parameterType
    if valueType?
      @type
    else
      "const #{@type}&"
    end
  end

  def hasComplexSetter?
    @onChange != nil
  end

  def hasComplexGetter?
    hasInspectorOverride?
  end

  def setterFunctionName
    if @name.start_with?("css", "xss", "ftp", "dom", "dns", "ice", "hdr")
      "set" + @name[0..2].upcase + @name[3..@name.length]
    else
      "set" + @name[0].upcase + @name[1..@name.length]
    end
  end

  def getterFunctionName
    @getter || @name
  end

  def hasInspectorOverride?
    @inspectorOverride == true
  end
end

class Conditional
  attr_accessor :condition
  attr_accessor :settings
  attr_accessor :settingsNeedingImplementation
  attr_accessor :boolSettings
  attr_accessor :boolSettingsNeedingImplementation
  attr_accessor :nonBoolSettings
  attr_accessor :nonBoolSettingsNeedingImplementation
  attr_accessor :settingsWithComplexGetters
  attr_accessor :settingsWithComplexGettersNeedingImplementation
  attr_accessor :settingsWithComplexSetters
  attr_accessor :settingsWithComplexSettersNeedingImplementation

  def initialize(condition, settings)
    @condition = condition

    @settings = settings
    @settingsNeedingImplementation = @settings.reject { |setting| setting.customImplementation }
    
    @boolSettings = @settings.select { |setting| setting.type == "bool" }
    @boolSettingsNeedingImplementation = @boolSettings.reject { |setting| setting.customImplementation }
  
    @nonBoolSettings = @settings.reject { |setting| setting.type == "bool" }
    @nonBoolSettingsNeedingImplementation = @nonBoolSettings.reject { |setting| setting.customImplementation }

    @settingsWithComplexGetters = @settings.select { |setting| setting.hasComplexGetter? }
    @settingsWithComplexGettersNeedingImplementation = @settingsWithComplexGetters.reject { |setting| setting.customImplementation }

    @settingsWithComplexSetters = @settings.select { |setting| setting.hasComplexSetter? }
    @settingsWithComplexSettersNeedingImplementation = @settingsWithComplexSetters.reject { |setting| setting.customImplementation }
  end
end

class SettingSet
  attr_accessor :settings
  attr_accessor :inspectorOverrideSettings
  attr_accessor :conditionals

  def initialize(settings)
    @settings = settings
    @settings.sort! { |x, y| x.name <=> y.name }

    @inspectorOverrideSettings = @settings.select { |setting| setting.hasInspectorOverride? }

    @conditionals = []
    conditionalsMap = {}
    @settings.select { |setting| setting.conditional }.each do |setting|
      if !conditionalsMap[setting.conditional]
        conditionalsMap[setting.conditional] = []
      end

      conditionalsMap[setting.conditional] << setting
    end
    conditionalsMap.each do |key, value|
      @conditionals << Conditional.new(key, value)
    end
    @conditionals.sort! { |x, y| x.condition <=> y.condition }

    # We also add the unconditional settings as the first element in the conditional array.
    @conditionals.unshift(Conditional.new(nil, @settings.reject { |setting| setting.conditional }))
  end
end

class Settings
  attr_accessor :allSettingsSet
  
  def initialize(parsedSettingsFromWebCore, parsedSettingsFromWebPreferences)
    settingsFromWebPreferences = []
    parsedSettingsFromWebPreferences.each do |name, options|
      # An empty "webcoreBinding" entry indicates this preference uses the default, which is bound to Settings.
      if !options["webcoreBinding"]
        settingsFromWebPreferences << SettingFromWebPreferences.new(name, options)
      end
    end

    settingsFromWebCore = []
    parsedSettingsFromWebCore.each do |name, options|
      settingsFromWebCore << SettingFromWebCore.new(name, options)
    end

    @allSettingsSet = SettingSet.new(settingsFromWebPreferences + settingsFromWebCore)
  end

  def renderToFile(template, file)
    template = File.join(File.dirname(__FILE__), template)

    output = ERB.new(File.read(template), 0, "-").result(binding)
    File.open(file, "w+") do |f|
      f.write(output)
    end
  end
end

settings = Settings.new(parsedSettings, parsedPreferences)
settings.renderToFile("SettingsTemplates/Settings.h.erb", File.join(options[:outputDirectory], "Settings.h"))
settings.renderToFile("SettingsTemplates/Settings.cpp.erb", File.join(options[:outputDirectory], "Settings.cpp"))
settings.renderToFile("SettingsTemplates/InternalSettingsGenerated.idl.erb", File.join(options[:outputDirectory], "InternalSettingsGenerated.idl"))
settings.renderToFile("SettingsTemplates/InternalSettingsGenerated.h.erb", File.join(options[:outputDirectory], "InternalSettingsGenerated.h"))
settings.renderToFile("SettingsTemplates/InternalSettingsGenerated.cpp.erb", File.join(options[:outputDirectory], "InternalSettingsGenerated.cpp"))
