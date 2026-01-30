/*
 * Copyright 2019 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

package com.google.googlesql;

import com.google.auto.value.AutoValue;
import com.google.common.base.Ascii;
import com.google.common.base.Preconditions;
import com.google.common.collect.HashBasedTable;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.Table;
import com.google.googlesql.GoogleSQLOptionsProto.AllowedHintsAndOptionsProto;
import com.google.googlesql.GoogleSQLOptionsProto.AllowedHintsAndOptionsProto.HintProto;
import com.google.googlesql.GoogleSQLOptionsProto.AllowedHintsAndOptionsProto.OptionProto;
import com.google.googlesql.GoogleSQLType.TypeKind;
import com.google.googlesql.GoogleSQLType.TypeProto;
import java.io.Serializable;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import javax.annotation.Nullable;

/**
 * This class specifies a set of allowed hints and options, and their expected types.
 *
 * <p>Each hint or option has an expected Type, which can be NULL. If the expected type is NULL,
 * then any type is allowed. If a type is specified, the resolved value for the hint will always
 * have the expected type, and the analyzer will give an error if coercion is not possible.
 *
 * <p>Hint, option and qualifier names are all case insensitive. The resolved AST will contain the
 * original case as written by the user.
 *
 * <p>The {@code disallow_duplicate_option_names} field will disallow duplicate option names for
 * non-anonymization and non-aggregation_threshold options. anonymization and aggregation_threshold
 * options already disallow duplicate option names by default.
 *
 * <p>The {@code disallow_unknown_options} and {@code disallow_unknown_hints_with_qualifiers} fields
 * can be set to indicate that errors should be given on unknown options or hints (with specific
 * qualifiers). Unknown hints with other qualifiers do not cause errors.
 */
public class AllowedHintsAndOptions implements Serializable {

  /** This class specifies a set of option type and resolving kind */
  @AutoValue
  public abstract static class AllowedOptionProperties {
    // Expected option type.
    @Nullable
    public abstract Type type();

    // Option resolving kind.
    public abstract OptionProto.ResolvingKind resolvingKind();

    public abstract boolean allowAlterArray();

    public abstract Builder toBuilder();

    public static Builder builder() {
      return new AutoValue_AllowedHintsAndOptions_AllowedOptionProperties.Builder();
    }

    /** Builder. */
    @AutoValue.Builder
    public abstract interface Builder {
      Builder type(@Nullable Type type);

      Builder resolvingKind(OptionProto.ResolvingKind referenceTree);

      Builder allowAlterArray(boolean allowAlterArray);

      AllowedOptionProperties build();
    }
  }

  // If true, give an error for an unknown option.
  boolean disallowUnknownOptions = false;
  // If true, give an error if an option name appears more than once in a single  options list.
  // Does not apply to anonymization and aggregate_threshold options.
  boolean disallowDuplicateOptionNames = false;
  // Keys are lower-case qualifiers, values are the original qualifiers.
  private final Map<String, String> disallowUnknownHintsWithQualifiers = new HashMap<>();
  // Table containing declared hints. Table keys are in lower-case. The key is
  // (qualifier, hint). Unqualified hints are declared using an empty qualifier.
  private final Table<String, String, Hint> hints = HashBasedTable.create();
  // Map containing declared options. Map keys are in lower-case.
  private final Map<String, AllowedOptionProperties> options = new HashMap<>();
  // Map containing declared anonymization options. Map keys are in lower-case.
  private final Map<String, AllowedOptionProperties> anonymizationOptions = new HashMap<>();
  // Map containing declared differential privacy options. Map keys are in lower-case.
  private final Map<String, AllowedOptionProperties> differentialPrivacyOptions = new HashMap<>();

  public AllowedHintsAndOptions() {
    addDefaultAnonymizationOptions();
    addDefaultDifferentialPrivacyOptions();
  }

  /**
   * This is recommended constructor to use for normal settings.
   *
   * <p>All supported hints and options should be added with the Add methods. <br>
   * Unknown options will be errors. <br>
   * Duplicate option names will be permitted (for non-anonymization and non-aggregation_threshold
   * options). <br>
   * Unknown hints without qualifiers, or with {@code qualifier}, will be errors. <br>
   * Unkonwn hints with other qualifiers will be allowed (because these are typically interpreted as
   * hints intended for other engines).
   */
  public AllowedHintsAndOptions(String qualifier) {
    addDefaultAnonymizationOptions();
    addDefaultDifferentialPrivacyOptions();
    disallowUnknownOptions = true;
    disallowDuplicateOptionNames = false;
    disallowUnknownHintsWithQualifier(qualifier);
    disallowUnknownHintsWithQualifier("");
  }

  /**
   * Serialize this AllowedHintsAndOptions into protobuf, with FileDescriptors emitted to the
   * builder as needed.
   */
  AllowedHintsAndOptionsProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    AllowedHintsAndOptionsProto.Builder builder = AllowedHintsAndOptionsProto.newBuilder();
    builder.setDisallowUnknownOptions(disallowUnknownOptions);
    builder.setDisallowDuplicateOptionNames(disallowDuplicateOptionNames);
    builder.addAllDisallowUnknownHintsWithQualifier(disallowUnknownHintsWithQualifiers.values());
    for (Hint hint : hints.values()) {
      if (hint.getQualifier().isEmpty() && !hint.getAllowUnqualified()) {
        continue;
      }
      if (hint.getType() != null) {
        TypeProto.Builder typeBuilder = TypeProto.newBuilder();
        hint.getType().serialize(typeBuilder, fileDescriptorSetsBuilder);
        builder
            .addHintBuilder()
            .setQualifier(hint.getQualifier())
            .setName(hint.getName())
            .setType(typeBuilder)
            .setAllowUnqualified(hint.getAllowUnqualified());
      } else {
        builder
            .addHintBuilder()
            .setQualifier(hint.getQualifier())
            .setName(hint.getName())
            .setAllowUnqualified(hint.getAllowUnqualified());
      }
    }
    for (Entry<String, AllowedOptionProperties> option : options.entrySet()) {
      if (option.getValue().type() != null) {
        TypeProto.Builder typeBuilder = TypeProto.newBuilder();
        option.getValue().type().serialize(typeBuilder, fileDescriptorSetsBuilder);
        builder
            .addOptionBuilder()
            .setName(option.getKey())
            .setType(typeBuilder)
            .setResolvingKind(option.getValue().resolvingKind())
            .setAllowAlterArray(option.getValue().allowAlterArray());
      } else {
        builder
            .addOptionBuilder()
            .setName(option.getKey())
            .setResolvingKind(option.getValue().resolvingKind())
            .setAllowAlterArray(option.getValue().allowAlterArray());
      }
    }
    for (Entry<String, AllowedOptionProperties> anonymizationOption :
        anonymizationOptions.entrySet()) {
      if (anonymizationOption.getValue().type() != null) {
        TypeProto.Builder typeBuilder = TypeProto.newBuilder();
        anonymizationOption.getValue().type().serialize(typeBuilder, fileDescriptorSetsBuilder);
        builder
            .addAnonymizationOptionBuilder()
            .setName(anonymizationOption.getKey())
            .setType(typeBuilder)
            .setResolvingKind(anonymizationOption.getValue().resolvingKind())
            .setAllowAlterArray(anonymizationOption.getValue().allowAlterArray());
      } else {
        builder
            .addAnonymizationOptionBuilder()
            .setName(anonymizationOption.getKey())
            .setResolvingKind(anonymizationOption.getValue().resolvingKind())
            .setAllowAlterArray(anonymizationOption.getValue().allowAlterArray());
      }
    }
    for (Entry<String, AllowedOptionProperties> differentialPrivacyOption :
        differentialPrivacyOptions.entrySet()) {
      if (differentialPrivacyOption.getValue().type() != null) {
        TypeProto.Builder typeBuilder = TypeProto.newBuilder();
        differentialPrivacyOption
            .getValue()
            .type()
            .serialize(typeBuilder, fileDescriptorSetsBuilder);
        builder
            .addDifferentialPrivacyOptionBuilder()
            .setName(differentialPrivacyOption.getKey())
            .setType(typeBuilder)
            .setResolvingKind(differentialPrivacyOption.getValue().resolvingKind())
            .setAllowAlterArray(differentialPrivacyOption.getValue().allowAlterArray());
      } else {
        builder
            .addDifferentialPrivacyOptionBuilder()
            .setName(differentialPrivacyOption.getKey())
            .setResolvingKind(differentialPrivacyOption.getValue().resolvingKind())
            .setAllowAlterArray(differentialPrivacyOption.getValue().allowAlterArray());
      }
    }
    return builder.build();
  }

  /**
   * Deserialize an AllowedHintsAndOptions from proto. Types will be created using given type
   * factory and descriptor pools.
   */
  static AllowedHintsAndOptions deserialize(
      AllowedHintsAndOptionsProto proto,
      List<? extends DescriptorPool> pools,
      TypeFactory factory) {
    AllowedHintsAndOptions allowed = new AllowedHintsAndOptions();
    allowed.anonymizationOptions.clear();
    allowed.differentialPrivacyOptions.clear();
    for (String qualifier : proto.getDisallowUnknownHintsWithQualifierList()) {
      Preconditions.checkArgument(
          !allowed.disallowUnknownHintsWithQualifiers.containsKey(Ascii.toLowerCase(qualifier)));
      allowed.disallowUnknownHintsWithQualifier(qualifier);
    }
    allowed.setDisallowUnknownOptions(proto.getDisallowUnknownOptions());
    allowed.setDisallowDuplicateOptionNames(proto.getDisallowDuplicateOptionNames());
    for (HintProto hint : proto.getHintList()) {
      if (hint.hasType()) {
        allowed.addHint(
            hint.getQualifier(),
            hint.getName(),
            factory.deserialize(hint.getType(), pools),
            hint.getAllowUnqualified());
      } else {
        allowed.addHint(hint.getQualifier(), hint.getName(), null, hint.getAllowUnqualified());
      }
    }
    for (OptionProto option : proto.getOptionList()) {
      OptionProto.ResolvingKind resolvingKind =
          option.hasResolvingKind()
              ? option.getResolvingKind()
              : OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER;
      if (option.hasType()) {
        allowed.addOption(
            option.getName(),
            factory.deserialize(option.getType(), pools),
            option.getAllowAlterArray(),
            resolvingKind);
      } else {
        allowed.addOption(option.getName(), null, option.getAllowAlterArray(), resolvingKind);
      }
    }

    // We want to include default options when we are deserializing value here if factory is not
    // present, since we will not be able to parse them e.g. this function is used in
    // AnalyzerOptions constructor with factory == null.
    if (factory == null) {
      allowed.addDefaultAnonymizationOptions();
      allowed.addDefaultDifferentialPrivacyOptions();
    } else {
      for (OptionProto anonymizationOption : proto.getAnonymizationOptionList()) {
        OptionProto.ResolvingKind resolvingKind =
            anonymizationOption.hasResolvingKind()
                ? anonymizationOption.getResolvingKind()
                : OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER;

        if (anonymizationOption.hasType()) {
          allowed.addAnonymizationOption(
              anonymizationOption.getName(),
              factory.deserialize(anonymizationOption.getType(), pools),
              resolvingKind);
        } else {
          allowed.addAnonymizationOption(anonymizationOption.getName(), null, resolvingKind);
        }
      }
      for (OptionProto differentialPrivacyOption : proto.getDifferentialPrivacyOptionList()) {
        OptionProto.ResolvingKind resolvingKind =
            differentialPrivacyOption.hasResolvingKind()
                ? differentialPrivacyOption.getResolvingKind()
                : OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER;
        if (differentialPrivacyOption.hasType()) {
          allowed.addDifferentialPrivacyOption(
              differentialPrivacyOption.getName(),
              factory.deserialize(differentialPrivacyOption.getType(), pools),
              resolvingKind);
        } else {
          allowed.addDifferentialPrivacyOption(
              differentialPrivacyOption.getName(), null, resolvingKind);
        }
      }
    }
    return allowed;
  }

  /**
   * Add an option with CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER resolvingKind.
   *
   * @param type may be NULL to indicate that all Types are allowed.
   */
  public void addOption(String name, @Nullable Type type, boolean allowAlterArray) {
    addOption(
        name,
        type,
        allowAlterArray,
        OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER);
  }

  /**
   * Add an option with CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER resolvingKind.
   *
   * @param type may be NULL to indicate that all Types are allowed.
   */
  public void addOption(String name, @Nullable Type type) {
    addOption(
        name,
        type,
        /* allowAlterArray= */ false,
        OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER);
  }

  /**
   * Add an option.
   *
   * @param type may be NULL to indicate that all Types are allowed.
   * @param resolvingKind may not be NULL.
   */
  public void addOption(
      String name,
      @Nullable Type type,
      boolean allowAlterArray,
      OptionProto.ResolvingKind resolvingKind) {
    Preconditions.checkNotNull(name);
    Preconditions.checkNotNull(resolvingKind);
    Preconditions.checkArgument(!name.isEmpty());
    Preconditions.checkArgument(!options.containsKey(Ascii.toLowerCase(name)));
    options.put(
        Ascii.toLowerCase(name),
        AllowedOptionProperties.builder()
            .type(type)
            .resolvingKind(resolvingKind)
            .allowAlterArray(allowAlterArray)
            .build());
  }

  /**
   * Add an anonymization option with CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER resolvingKind.
   *
   * @param type may be NULL to indicate that all Types are allowed.
   */
  public void addAnonymizationOption(String name, @Nullable Type type) {
    addAnonymizationOption(
        name, type, OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER);
  }

  /**
   * Add an anonymization option.
   *
   * @param type may be NULL to indicate that all Types are allowed.
   * @param resolvingKind may not be NULL.
   */
  public void addAnonymizationOption(
      String name, @Nullable Type type, OptionProto.ResolvingKind resolvingKind) {
    Preconditions.checkNotNull(name);
    Preconditions.checkNotNull(resolvingKind);
    Preconditions.checkArgument(!name.isEmpty());
    Preconditions.checkArgument(!anonymizationOptions.containsKey(Ascii.toLowerCase(name)));
    anonymizationOptions.put(
        Ascii.toLowerCase(name),
        AllowedOptionProperties.builder()
            .type(type)
            .resolvingKind(resolvingKind)
            .allowAlterArray(false)
            .build());
  }

  /**
   * Add a differential privacy option with CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER resolvingKind.
   *
   * @param type may be NULL to indicate that all Types are allowed.
   */
  public void addDifferentialPrivacyOption(String name, @Nullable Type type) {
    addDifferentialPrivacyOption(
        name, type, OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER);
  }

  /**
   * Add a differential privacy option.
   *
   * @param type may be NULL to indicate that all Types are allowed.
   * @param resolvingKind may not be NULL.
   */
  public void addDifferentialPrivacyOption(
      String name, @Nullable Type type, OptionProto.ResolvingKind resolvingKind) {
    Preconditions.checkNotNull(name);
    Preconditions.checkNotNull(resolvingKind);
    Preconditions.checkArgument(!name.isEmpty());
    Preconditions.checkArgument(!differentialPrivacyOptions.containsKey(Ascii.toLowerCase(name)));
    differentialPrivacyOptions.put(
        Ascii.toLowerCase(name),
        AllowedOptionProperties.builder()
            .type(type)
            .resolvingKind(resolvingKind)
            .allowAlterArray(false)
            .build());
  }

  /**
   * Add a hint.
   *
   * @param qualifier may be empty to add this hint only unqualified, but hints for some engine
   *     should normally allow the engine name as a qualifier.
   * @param name
   * @param type may be NULL to indicate that all Types are allowed.
   * @param allowUnqualified if true, this hint is allowed both unqualified and qualified with
   *     {@code qualifier}.
   */
  public void addHint(
      String qualifier, String name, @Nullable Type type, boolean allowUnqualified) {
    Preconditions.checkNotNull(name);
    Preconditions.checkArgument(!name.isEmpty());
    Preconditions.checkNotNull(qualifier);
    Preconditions.checkArgument(
        !qualifier.isEmpty() || allowUnqualified,
        "Cannot have hint with no qualifier and !allowUnqualified");
    Preconditions.checkArgument(
        !hints.contains(Ascii.toLowerCase(qualifier), Ascii.toLowerCase(name)));
    hints.put(
        Ascii.toLowerCase(qualifier),
        Ascii.toLowerCase(name),
        new Hint(qualifier, name, type, allowUnqualified));
    if (allowUnqualified && !qualifier.isEmpty()) {
      Preconditions.checkArgument(!hints.contains("", Ascii.toLowerCase(name)));
      hints.put(
          "" /* Unqualified hints use empty qualifier */,
          Ascii.toLowerCase(name),
          new Hint(
              "" /* Unqualified hints use empty qualifier */,
              name,
              type,
              false /* Marks that this hint is not explicitly added and won't be serialized */));
    }
  }

  public void setDisallowUnknownOptions(boolean disallowUnknownOptions) {
    this.disallowUnknownOptions = disallowUnknownOptions;
  }

  public boolean getDisallowUnknownOptions() {
    return disallowUnknownOptions;
  }

  public void setDisallowDuplicateOptionNames(boolean disallowDuplicateOptionNames) {
    this.disallowDuplicateOptionNames = disallowDuplicateOptionNames;
  }

  public boolean getDisallowDuplicateOptionNames() {
    return disallowDuplicateOptionNames;
  }

  public Hint getHint(String qualifier, String name) {
    return hints.get(Ascii.toLowerCase(qualifier), Ascii.toLowerCase(name));
  }

  public ImmutableList<Hint> getHintList() {
    return ImmutableList.copyOf(hints.values());
  }

  @Nullable
  public Type getOptionType(String name) {
    AllowedOptionProperties optionProperties = options.get(Ascii.toLowerCase(name));
    return optionProperties == null ? null : optionProperties.type();
  }

  @Nullable
  public Type getAnonymizationOptionType(String name) {
    AllowedOptionProperties optionProperties = anonymizationOptions.get(Ascii.toLowerCase(name));
    return optionProperties == null ? null : optionProperties.type();
  }

  @Nullable
  public Type getDifferentialPrivacyOptionType(String name) {
    AllowedOptionProperties optionProperties =
        differentialPrivacyOptions.get(Ascii.toLowerCase(name));
    return optionProperties == null ? null : optionProperties.type();
  }

  public ImmutableList<String> getOptionNameList() {
    return ImmutableList.copyOf(options.keySet());
  }

  public ImmutableList<String> getAnonymizationOptionNameList() {
    return ImmutableList.copyOf(anonymizationOptions.keySet());
  }

  public ImmutableList<String> getDifferentialPrivacyOptionNameList() {
    return ImmutableList.copyOf(differentialPrivacyOptions.keySet());
  }

  public void disallowUnknownHintsWithQualifier(String qualifier) {
    Preconditions.checkNotNull(qualifier);
    Preconditions.checkArgument(
        !disallowUnknownHintsWithQualifiers.containsKey(Ascii.toLowerCase(qualifier)));
    disallowUnknownHintsWithQualifiers.put(Ascii.toLowerCase(qualifier), qualifier);
  }

  public ImmutableList<String> getDisallowUnknownHintsWithQualifiers() {
    return ImmutableList.copyOf(disallowUnknownHintsWithQualifiers.values());
  }

  /** This class specifies all information of a hint. */
  public static class Hint implements Serializable {
    private String qualifier;
    private String name;
    private Type type;
    private boolean allowUnqualified;

    Hint(String qualifier, String name, @Nullable Type type, boolean allowUnqualified) {
      this.qualifier = qualifier;
      this.name = name;
      this.type = type;
      this.allowUnqualified = allowUnqualified;
    }

    public String getQualifier() {
      return qualifier;
    }

    public String getName() {
      return name;
    }

    @Nullable
    public Type getType() {
      return type;
    }

    /**
     * AllowUnqualified is only used when adding new hints and serializing to proto to recreate
     * hints in C++ side. This should only be used inside the class and should not be used by users.
     */
    private boolean getAllowUnqualified() {
      return allowUnqualified;
    }
  }

  private void addDefaultAnonymizationOptions() {
    anonymizationOptions.put(
        "epsilon",
        AllowedOptionProperties.builder()
            .type(TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE))
            .resolvingKind(OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER)
            .allowAlterArray(false)
            .build());
    anonymizationOptions.put(
        "delta",
        AllowedOptionProperties.builder()
            .type(TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE))
            .resolvingKind(OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER)
            .allowAlterArray(false)
            .build());
    anonymizationOptions.put(
        "k_threshold",
        AllowedOptionProperties.builder()
            .type(TypeFactory.createSimpleType(TypeKind.TYPE_INT64))
            .resolvingKind(OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER)
            .allowAlterArray(false)
            .build());
    anonymizationOptions.put(
        "kappa",
        AllowedOptionProperties.builder()
            .type(TypeFactory.createSimpleType(TypeKind.TYPE_INT64))
            .resolvingKind(OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER)
            .allowAlterArray(false)
            .build());
    anonymizationOptions.put(
        "max_rows_contributed",
        AllowedOptionProperties.builder()
            .type(TypeFactory.createSimpleType(TypeKind.TYPE_INT64))
            .resolvingKind(OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER)
            .allowAlterArray(false)
            .build());
    anonymizationOptions.put(
        "min_privacy_units_per_group",
        AllowedOptionProperties.builder()
            .type(TypeFactory.createSimpleType(TypeKind.TYPE_INT64))
            .resolvingKind(OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER)
            .allowAlterArray(false)
            .build());
  }

  // Helper function populates differentialPrivacyOptions with default set of options that are
  // accepted. Note that these options are different than anonymization options above see:
  // (broken link).
  private void addDefaultDifferentialPrivacyOptions() {
    differentialPrivacyOptions.put(
        "epsilon",
        AllowedOptionProperties.builder()
            .type(TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE))
            .resolvingKind(OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER)
            .allowAlterArray(false)
            .build());
    differentialPrivacyOptions.put(
        "delta",
        AllowedOptionProperties.builder()
            .type(TypeFactory.createSimpleType(TypeKind.TYPE_DOUBLE))
            .resolvingKind(OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER)
            .allowAlterArray(false)
            .build());
    differentialPrivacyOptions.put(
        "max_groups_contributed",
        AllowedOptionProperties.builder()
            .type(TypeFactory.createSimpleType(TypeKind.TYPE_INT64))
            .resolvingKind(OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER)
            .allowAlterArray(false)
            .build());
    differentialPrivacyOptions.put(
        "max_rows_contributed",
        AllowedOptionProperties.builder()
            .type(TypeFactory.createSimpleType(TypeKind.TYPE_INT64))
            .resolvingKind(OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER)
            .allowAlterArray(false)
            .build());
    differentialPrivacyOptions.put(
        "min_privacy_units_per_group",
        AllowedOptionProperties.builder()
            .type(TypeFactory.createSimpleType(TypeKind.TYPE_INT64))
            .resolvingKind(OptionProto.ResolvingKind.CONSTANT_OR_EMPTY_NAME_SCOPE_IDENTIFIER)
            .allowAlterArray(false)
            .build());
  }
}
