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

import com.google.common.base.Ascii;
import com.google.common.base.Preconditions;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.errorprone.annotations.CanIgnoreReturnValue;
import com.google.googlesql.DescriptorPool.GoogleSQLDescriptor;
import com.google.googlesql.DescriptorPool.GoogleSQLEnumDescriptor;
import com.google.googlesql.FunctionProtos.FunctionProto;
import com.google.googlesql.FunctionProtos.ProcedureProto;
import com.google.googlesql.FunctionProtos.TableValuedFunctionProto;
import com.google.googlesql.GoogleSQLOptionsProto.GoogleSQLBuiltinFunctionOptionsProto;
import com.google.googlesql.GoogleSQLType.TypeProto;
import com.google.googlesql.LocalService.GetBuiltinFunctionsResponse;
import com.google.googlesql.LocalService.RegisterCatalogRequest;
import com.google.googlesql.LocalService.RegisterResponse;
import com.google.googlesql.LocalService.UnregisterRequest;
import com.google.googlesql.SimpleCatalogProtos.SimpleCatalogProto;
import com.google.googlesql.SimpleCatalogProtos.SimpleCatalogProto.NamedTypeProto;
import com.google.googlesql.SimpleConnectionProtos.SimpleConnectionProto;
import com.google.googlesql.SimpleConstantProtos.SimpleConstantProto;
import com.google.googlesql.SimpleModelProtos.SimpleModelProto;
import com.google.googlesql.SimplePropertyGraphProtos.SimplePropertyGraphProto;
import com.google.googlesql.SimpleSequenceProtos.SimpleSequenceProto;
import com.google.googlesql.SimpleTableProtos.SimpleTableProto;
import io.grpc.StatusRuntimeException;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Map.Entry;
import java.util.Set;
import java.util.logging.Logger;
import javax.annotation.Nullable;

/**
 * SimpleCatalog is a concrete implementation of the Catalog interface. It acts as a simple
 * container for objects in the Catalog.
 *
 * <p>This class and related SimpleX classes are final by design because a subclass is unlikely to
 * get serialize method right. The serialize method returns a SimpleCatalogProto. A subclass most
 * likely needs more fields and hence needs a different proto. But proto class is final so a
 * subclass cannot override this class's serialize method and return a subclass of
 * SimpleCatalogProto. TODO: Add findTableById.
 *
 * <p>To improve RPC performance, a SimpleCatalog can be registered on the local server, and later
 * requests involving this SimpleCatalog will only pass the registered ID instead of the fully
 * serialized form. A registered SimpleCatalog can and should be unregistered when it is no longer
 * used. The unregister() method is called in finalize(), but users shouldn't rely on that because
 * the JVM doesn't know of the memory resource taken by the registered catalog on the local server,
 * and may start the garbage collector later than necessary.
 */
public class SimpleCatalog extends Catalog {
  private static final Logger logger = Logger.getLogger(SimpleCatalog.class.getName());

  private final String name;
  private final TypeFactory typeFactory;
  // All String keys are stored in lower case.
  private final Map<String, Constant> constants = new HashMap<>();
  private final Map<String, SimpleTable> tables = new HashMap<>();
  private final Map<String, Type> types = new HashMap<>();
  private final Map<String, SimpleCatalog> catalogs = new HashMap<>();
  private final Map<Long, SimpleTable> tablesById = new HashMap<>();
  private final Map<String, Function> customFunctions = new HashMap<>();
  private final Map<String, TableValuedFunction> customTvfs = new HashMap<>();
  private final Map<String, Function> functionsByFullName = new HashMap<>();
  private final Map<String, TableValuedFunction> tvfsByFullName = new HashMap<>();
  private final Map<String, Procedure> procedures = new HashMap<>();
  // globalNames is used to avoid naming conflict of top tier objects including
  // tables and property graphs.
  // When inserting into them, insert the name into global_names as well.
  private final Set<String> globalNames = new HashSet<>();
  private final Map<String, SimplePropertyGraph> propertyGraphs = new HashMap<>();
  private final Map<String, SimpleConnection> connections = new HashMap<>();
  private final Map<String, SimpleModel> models = new HashMap<>();
  private final Map<Long, SimpleModel> modelsById = new HashMap<>();
  private final Map<String, SimpleSequence> sequences = new HashMap<>();
  private DescriptorPool descriptorPool;
  GoogleSQLBuiltinFunctionOptionsProto builtinFunctionOptions;

  boolean registered = false;
  long registeredId = -1;
  FileDescriptorSetsBuilder registeredFileDescriptorSetsBuilder = null;
  // If the catalog is registered, contains the descriptor pool ids that should be
  // used to serialize it in the future.
  ImmutableMap<DescriptorPool, Long> registeredDescriptorPoolIds = null;

  public SimpleCatalog(String name, TypeFactory typeFactory) {
    this.name = name;
    this.typeFactory = Preconditions.checkNotNull(typeFactory);
  }

  public SimpleCatalog(String name) {
    this(name, TypeFactory.nonUniqueNames());
  }

  /**
   * Register this catalog to local server, so that it can be reused without passing through RPC
   * every time.
   *
   * @return An AutoCloseable object that can be used to unregister the catalog in
   *     try-with-resources structure.
   */
  @CanIgnoreReturnValue // TODO: consider removing this?
  public AutoUnregister register() {
    return register(ImmutableMap.of());
  }

  /**
   * Register this catalog to local server, so that it can be reused without passing through RPC
   * every time.
   *
   * @param tablesContents The contents of the tables owned by this catalog. The key is the name of
   *     table, while the value is content for that table.
   * @return An AutoCloseable object that can be used to unregister the catalog in
   *     try-with-resources structure.
   */
  @CanIgnoreReturnValue // TODO: consider removing this?
  public AutoUnregister register(Map<String, TableContent> tablesContents) {
    Preconditions.checkState(!registered);
    try {
      RegisterResponse resp =
          Client.getStub().registerCatalog(createRegisterRequest(tablesContents));
      processRegisterResponse(resp);
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }

    return new AutoUnregister();
  }

  protected RegisterCatalogRequest createRegisterRequest() {
    return createRegisterRequest(ImmutableMap.of());
  }

  protected RegisterCatalogRequest createRegisterRequest(Map<String, TableContent> tablesContents) {
    Preconditions.checkState(!registered);
    Preconditions.checkNotNull(tablesContents);
    registeredFileDescriptorSetsBuilder = new FileDescriptorSetsBuilder();
    RegisterCatalogRequest.Builder builder = RegisterCatalogRequest.newBuilder();
    builder.setSimpleCatalog(serialize(registeredFileDescriptorSetsBuilder));
    tablesContents.forEach((t, c) -> builder.putTableContent(t, c.serialize()));
    registeredFileDescriptorSetsBuilder.build();
    builder.setDescriptorPoolList(
        DescriptorPoolSerializer.createDescriptorPoolList(registeredFileDescriptorSetsBuilder));
    return builder.build();
  }

  protected void processRegisterResponse(RegisterResponse resp) {
    registeredId = resp.getRegisteredId();
    ImmutableMap.Builder<DescriptorPool, Long> idsMapBuilder = ImmutableMap.builder();
    ImmutableList<DescriptorPool> pools = registeredFileDescriptorSetsBuilder.getDescriptorPools();
    List<Long> poolIds = resp.getDescriptorPoolIdList().getRegisteredIdsList();
    Preconditions.checkState(pools.size() == poolIds.size());
    for (int i = 0; i < pools.size(); ++i) {
      idsMapBuilder.put(pools.get(i), poolIds.get(i));
    }
    registeredDescriptorPoolIds = idsMapBuilder.buildOrThrow();
    registered = true;
  }

  public void unregister() {
    Preconditions.checkState(registered);
    try {
      Client.getStub()
          .unregisterCatalog(UnregisterRequest.newBuilder().setRegisteredId(registeredId).build());
    } catch (StatusRuntimeException e) {
      // Maybe caused by double unregistering (in race conditions) or RPC
      // failure. The latter may cause leak but it's likely due to more serious
      // problems and there is no good way to recover. Just log and ignore.
      logger.severe("Failed to unregister catalog: " + e.getMessage());
    } finally {
      registered = false;
      registeredId = -1;
      registeredFileDescriptorSetsBuilder = null;
      registeredDescriptorPoolIds = null;
    }
  }

  public boolean isRegistered() {
    return registered;
  }

  long getRegisteredId() {
    Preconditions.checkState(registered);
    return registeredId;
  }

  @Override
  protected void finalize() {
    if (registered) {
      unregister();
    }
  }

  FileDescriptorSetsBuilder getRegisteredFileDescriptorSetsBuilder() {
    Preconditions.checkState(registered);
    return registeredFileDescriptorSetsBuilder;
  }

  Map<DescriptorPool, Long> getRegisteredDescriptorPoolIds() {
    if (registeredDescriptorPoolIds == null) {
      return ImmutableMap.of();
    }
    return ImmutableMap.copyOf(registeredDescriptorPoolIds);
  }

  /**
   * Serialize this catalog into protobuf, with FileDescriptors emitted to the builder as needed.
   */
  @CanIgnoreReturnValue // TODO: consider removing this?
  public SimpleCatalogProto serialize(FileDescriptorSetsBuilder fileDescriptorSetsBuilder) {
    SimpleCatalogProto.Builder builder = SimpleCatalogProto.newBuilder();
    builder.setName(name);
    // The built-in function definitions are not serialized. Instead, the BuiltinFunctionOptions
    // which specify which functions to include and exclude will be serialized, and the C++
    // deserialization will recreate the same built-in function signatures according to this.
    if (builtinFunctionOptions != null) {
      builder.setBuiltinFunctionOptions(builtinFunctionOptions);
    }
    for (Entry<String, SimpleTable> table : tables.entrySet()) {
      SimpleTableProto.Builder tableBuilder = builder.addTableBuilder();
      tableBuilder.mergeFrom(table.getValue().serialize(fileDescriptorSetsBuilder));
      if (!table.getKey().equals(Ascii.toLowerCase(tableBuilder.getName()))) {
        tableBuilder.setNameInCatalog(table.getKey());
      }
    }
    for (Entry<String, Type> type : types.entrySet()) {
      TypeProto.Builder typeProtoBuilder = TypeProto.newBuilder();
      type.getValue().serialize(typeProtoBuilder, fileDescriptorSetsBuilder);
      builder.addNamedType(
          NamedTypeProto.newBuilder().setName(type.getKey()).setType(typeProtoBuilder.build()));
    }
    for (Entry<String, SimpleCatalog> catalog : catalogs.entrySet()) {
      builder.addCatalog(catalog.getValue().serialize(fileDescriptorSetsBuilder));
    }
    for (Entry<String, TableValuedFunction> tvf : customTvfs.entrySet()) {
      builder.addCustomTvf(tvf.getValue().serialize(fileDescriptorSetsBuilder));
    }

    for (Entry<String, SimpleConnection> connection : connections.entrySet()) {
      builder.addConnection(connection.getValue().serialize());
    }

    for (Entry<String, Constant> constant : constants.entrySet()) {
      builder.addConstant(constant.getValue().serialize(fileDescriptorSetsBuilder));
    }

    for (Entry<String, SimpleModel> model : models.entrySet()) {
      builder.addModel(model.getValue().serialize(fileDescriptorSetsBuilder));
    }

    for (Entry<String, Function> function : customFunctions.entrySet()) {
      builder.addCustomFunction(function.getValue().serialize(fileDescriptorSetsBuilder));
    }

    for (Entry<String, Procedure> procedure : procedures.entrySet()) {
      builder.addProcedure(procedure.getValue().serialize(fileDescriptorSetsBuilder));
    }

    for (Entry<String, SimplePropertyGraph> propertyGraph : propertyGraphs.entrySet()) {
      builder.addPropertyGraph(propertyGraph.getValue().serialize(fileDescriptorSetsBuilder));
    }

    for (Entry<String, SimpleSequence> sequence : sequences.entrySet()) {
      builder.addSequence(sequence.getValue().serialize());
    }

    if (descriptorPool != null) {
      builder.setFileDescriptorSetIndex(
          fileDescriptorSetsBuilder.addAllFileDescriptors(descriptorPool));
    }

    return builder.build();
  }

  /** Add simple connection into this catalog. Connection names are case insensitive. */
  public void addConnection(SimpleConnection connection) {
    Preconditions.checkState(!registered);
    Preconditions.checkNotNull(connection.getFullName());
    Preconditions.checkArgument(
        !connections.containsKey(Ascii.toLowerCase(connection.getFullName())));
    String fullName = connection.getFullName();
    connections.put(Ascii.toLowerCase(fullName), connection);
  }

  /** Add simple constant into this catalog. Constant names are case insensitive. */
  public void addConstant(Constant constant) {
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(!constants.containsKey(Ascii.toLowerCase(constant.getFullName())));
    List<String> namePath = constant.getNamePath();
    Preconditions.checkArgument(!namePath.isEmpty());
    constants.put(Ascii.toLowerCase(namePath.get(namePath.size() - 1)), constant);
  }

  /** Add simple model into this catalog. Model names are case insensitive. */
  public void addModel(SimpleModel model) {
    Preconditions.checkState(!registered);
    Preconditions.checkNotNull(model.getFullName());
    Preconditions.checkArgument(!models.containsKey(Ascii.toLowerCase(model.getFullName())));
    Preconditions.checkArgument(!modelsById.containsKey(model.getId()));
    String fullName = model.getFullName();
    models.put(Ascii.toLowerCase(fullName), model);
    modelsById.put(model.getId(), model);
  }

  public void addSequence(SimpleSequence sequence) {
    Preconditions.checkState(!registered);
    Preconditions.checkNotNull(sequence.getFullName());
    Preconditions.checkArgument(!sequences.containsKey(Ascii.toLowerCase(sequence.getFullName())));
    String fullName = sequence.getFullName();
    sequences.put(Ascii.toLowerCase(fullName), sequence);
  }

  /**
   * Add simple table into this catalog. Table names are case insensitive.
   *
   * @param table
   */
  public void addSimpleTable(SimpleTable table) {
    addSimpleTable(table.getName(), table);
  }

  public void addSimpleTable(String name, SimpleTable table) {
    String nameInLowerCase = Ascii.toLowerCase(name);
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(!tables.containsKey(nameInLowerCase), "duplicate key: %s", name);
    Preconditions.checkArgument(!globalNames.contains(nameInLowerCase), "duplicate key: %s", name);
    tables.put(nameInLowerCase, table);
    globalNames.add(nameInLowerCase);
    tablesById.put(table.getId(), table);
  }

  /** Removes a simple table from this catalog. */
  public void removeSimpleTable(SimpleTable table) {
    removeSimpleTable(table.getName());
  }

  /** Removes a simple table from this catalog. Table names are case insensitive. */
  public void removeSimpleTable(String name) {
    String nameInLowerCase = Ascii.toLowerCase(name);
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(tables.containsKey(nameInLowerCase), "missing key: %s", name);
    Preconditions.checkArgument(globalNames.contains(nameInLowerCase), "missing key: %s", name);
    SimpleTable table = tables.remove(nameInLowerCase);
    tablesById.remove(table.getId());
    globalNames.remove(nameInLowerCase);
  }

  /**
   * Add type into this catalog. Type names are case insensitive.
   *
   * @param name
   * @param type
   */
  public void addType(String name, Type type) {
    addTypeImpl(name, type, false);
  }

  private void addTypeImpl(String name, Type type, boolean allowDuplicateEntries) {
    String nameInLowerCase = Ascii.toLowerCase(name);
    Preconditions.checkState(!registered);
    Type existingType = types.get(nameInLowerCase);
    if (existingType != null) {
      // This can happen during deserialization when a type from the catalog
      // matches a builtin type.
      Preconditions.checkArgument(
          allowDuplicateEntries && existingType.equals(type),
          "duplicate type: %s and types do not match, while adding. Existing Type %s New type %s",
          name,
          existingType,
          type);
      return;
    }
    types.put(nameInLowerCase, type);
  }

  /** Removes the type with the passed name from this catalog. Type names are case insensitive. */
  public void removeType(String name) {
    String nameInLowerCase = Ascii.toLowerCase(name);
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(types.containsKey(nameInLowerCase), "missing key: %s", name);
    types.remove(nameInLowerCase);
  }

  /** Removes the provided connection from this catalog. */
  public void removeConnection(SimpleConnection connection) {
    removeConnection(connection.getName());
  }

  /**
   * Removes the connection with the provided name from this catalog. Names are case insensitive.
   */
  public void removeConnection(String name) {
    String nameInLowerCase = Ascii.toLowerCase(name);
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(connections.containsKey(nameInLowerCase), "missing key: %s", name);
    connections.remove(nameInLowerCase);
  }

  /** Removes the provided model from this catalog. */
  public void removeModel(SimpleModel model) {
    removeModel(model.getFullName());
  }

  /** Removes the model with the provided fullname from this catalog. Names are case insensitive. */
  public void removeModel(String fullName) {
    String nameInLowerCase = Ascii.toLowerCase(fullName);
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(models.containsKey(nameInLowerCase), "missing key: %s", name);
    SimpleModel model = models.remove(nameInLowerCase);
    modelsById.remove(model.getId());
  }

  /** Add property graph into this catalog. Property graph names are case insensitive. */
  public void addSimplePropertyGraph(SimplePropertyGraph propertyGraph) {
    String nameInLowerCase = Ascii.toLowerCase(propertyGraph.getName());
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(!globalNames.contains(nameInLowerCase), "duplicate key: %s", name);
    Preconditions.checkArgument(
        !propertyGraphs.containsKey(nameInLowerCase), "duplicate key: %s", name);
    propertyGraphs.put(nameInLowerCase, propertyGraph);
    globalNames.add(nameInLowerCase);
  }

  /** Removes a property graph from this catalog. */
  public void removePropertyGraph(PropertyGraph propertyGraph) {
    removePropertyGraph(propertyGraph.getName());
  }

  /**
   * Removes a simple property graph from this catalog. Property graph names are case insensitive.
   */
  public void removePropertyGraph(String name) {
    String nameInLowerCase = Ascii.toLowerCase(name);
    Preconditions.checkState(!registered);
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(!globalNames.contains(nameInLowerCase), "missing key: %s", name);
    Preconditions.checkArgument(
        propertyGraphs.containsKey(nameInLowerCase), "missing key: %s", name);
    propertyGraphs.remove(nameInLowerCase);
    globalNames.remove(nameInLowerCase);
  }

  /**
   * Add sub catalog into this catalog. Catalog names are case insensitive.
   *
   * @param catalog
   */
  public void addSimpleCatalog(SimpleCatalog catalog) {
    String fullNameInLowerCase = Ascii.toLowerCase(catalog.getFullName());
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(
        !catalogs.containsKey(fullNameInLowerCase), "duplicate key: %s", catalog.getFullName());
    catalogs.put(fullNameInLowerCase, catalog);
  }

  /**
   * Create a new catalog and add the new catalog into this catalog and return the new sub catalog.
   * Catalog names are case insensitive.
   *
   * @param name
   * @return the created new sub catalog
   */
  @CanIgnoreReturnValue // TODO: consider removing this?
  public SimpleCatalog addNewSimpleCatalog(String name) {
    Preconditions.checkState(!registered);
    SimpleCatalog newCatalog = new SimpleCatalog(name, getTypeFactory());
    addSimpleCatalog(newCatalog);
    return newCatalog;
  }

  /** Removes the passed sub catalog from this catalog. */
  public void removeSimpleCatalog(SimpleCatalog catalog) {
    removeSimpleCatalog(catalog.getFullName());
  }

  /**
   * Removes the sub catalog with the passed name from this catalog. Catalog names are case
   * insensitive.
   */
  public void removeSimpleCatalog(String fullName) {
    String fullNameInLowerCase = Ascii.toLowerCase(fullName);
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(
        catalogs.containsKey(fullNameInLowerCase), "missing key: %s", fullName);
    catalogs.remove(fullNameInLowerCase);
  }

  /**
   * Add GoogleSQL built-in function definitions into this catalog. Function names are case
   * insensitive.
   *
   * @param options used to select which functions get loaded.
   */
  @Deprecated
  public void addGoogleSQLFunctions(GoogleSQLBuiltinFunctionOptions options) {
    addGoogleSQLFunctionsAndTypes(options);
  }

  /**
   * Add GoogleSQL built-in function definitions into this catalog. Function names are case
   * insensitive.
   *
   * @param options used to select which functions get loaded.
   */
  public void addGoogleSQLFunctionsAndTypes(GoogleSQLBuiltinFunctionOptions options) {
    Preconditions.checkNotNull(options);
    Preconditions.checkState(builtinFunctionOptions == null);
    builtinFunctionOptions = options.serialize();

    try {
      GetBuiltinFunctionsResponse response =
          Client.getStub().getBuiltinFunctions(builtinFunctionOptions);

      processGetBuiltinFunctionsResponse(response);
    } catch (StatusRuntimeException e) {
      throw new SqlException(e);
    }
  }

  void processGetBuiltinFunctionsResponse(GetBuiltinFunctionsResponse response) {
    ImmutableList<DescriptorPool> pools = ImmutableList.of(BuiltinDescriptorPool.getInstance());
    for (FunctionProto proto : response.getFunctionList()) {
      addFunctionToFullNameMap(Function.deserialize(proto, pools));
    }
    for (TableValuedFunctionProto proto : response.getTableValuedFunctionList()) {
      addTableValuedFunctionToFullNameMap(
          TableValuedFunction.deserialize(proto, pools, typeFactory));
    }
    for (Map.Entry<String, TypeProto> entry : response.getTypesMap().entrySet()) {
      Type type = typeFactory.deserialize(entry.getValue(), pools);
      // When we deserialize a catalog, it may contain these types already, so as any existing
      // type is equal to what we are about to add, ignore it.
      addTypeImpl(entry.getKey(), type, /*ignoreDuplicateEntries*/ true);
    }
  }

  /**
   * Add function into this catalog. Function name and alias name will both be stored if different.
   * Names are case insensitive.
   *
   * @param function
   */
  public void addFunction(Function function) {
    String nameInLowerCase = Ascii.toLowerCase(function.getName());
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(!customFunctions.containsKey(nameInLowerCase));
    customFunctions.put(nameInLowerCase, function);
    if (!function.getOptions().getAliasName().isEmpty()
        && !function.getOptions().getAliasName().equals(function.getName())) {
      String aliasInLowerCase = Ascii.toLowerCase(function.getOptions().getAliasName());
      Preconditions.checkArgument(!customFunctions.containsKey(aliasInLowerCase));
      customFunctions.put(aliasInLowerCase, function);
    }
    addFunctionToFullNameMap(function);
  }

  private void addFunctionToFullNameMap(Function function) {
    String functionName = Ascii.toLowerCase(function.getFullName());
    Preconditions.checkArgument(
        !functionsByFullName.containsKey(functionName), "%s already exists.", functionName);
    functionsByFullName.put(functionName, function);
  }

  private void addTableValuedFunctionToFullNameMap(TableValuedFunction tvf) {
    String tvfName = Ascii.toLowerCase(tvf.getFullName());
    Preconditions.checkArgument(
        !tvfsByFullName.containsKey(tvfName), "%s already exists.", tvfName);
    tvfsByFullName.put(tvfName, tvf);
  }

  /** Removes the passed function from this catalog. */
  public void removeFunction(Function function) {
    removeFunction(function.getFullName());
  }

  /** Removes the function with the passed full name from this catalog. */
  public void removeFunction(String fullName) {
    Preconditions.checkState(!registered);
    Function function = getFunctionByFullName(fullName);
    Preconditions.checkArgument(function != null);
    String functionNameInLowerCase = Ascii.toLowerCase(function.getName());
    Preconditions.checkArgument(customFunctions.containsKey(functionNameInLowerCase));
    customFunctions.remove(functionNameInLowerCase);
    if (!function.getOptions().getAliasName().isEmpty()) {
      customFunctions.remove(Ascii.toLowerCase(function.getOptions().getAliasName()));
    }
    removeFunctionFromFullNameMap(function);
  }

  /** Add the given {@code tvf} to this catalog. Names are case insensitive. */
  public void addTableValuedFunction(TableValuedFunction tvf) {
    Preconditions.checkState(!registered);
    String tvfName = Ascii.toLowerCase(tvf.getName());
    Preconditions.checkArgument(!customTvfs.containsKey(tvfName));
    customTvfs.put(tvfName, tvf);
    addTableValuedFunctionToFullNameMap(tvf);
  }

  /** Removes the passed TVF from this catalog. */
  public void removeTableValuedFunction(TableValuedFunction tvf) {
    removeTableValuedFunction(tvf.getFullName());
  }

  /** Removes the TVF with the passed full name from this catalog. */
  public void removeTableValuedFunction(String fullName) {
    Preconditions.checkState(!registered);
    TableValuedFunction tvf = getTvfByFullName(fullName);
    Preconditions.checkArgument(tvf != null);
    customTvfs.remove(Ascii.toLowerCase(tvf.getName()));
    removeTableValuedFunctionFromFullNameMap(tvf);
  }

  private void removeFunctionFromFullNameMap(Function function) {
    String fullNameInlowerCase = Ascii.toLowerCase(function.getFullName());
    Preconditions.checkArgument(functionsByFullName.containsKey(fullNameInlowerCase));
    functionsByFullName.remove(fullNameInlowerCase);
  }

  private void removeTableValuedFunctionFromFullNameMap(TableValuedFunction tvf) {
    String fullNameInlowerCase = Ascii.toLowerCase(tvf.getFullName());
    Preconditions.checkArgument(tvfsByFullName.containsKey(fullNameInlowerCase));
    tvfsByFullName.remove(fullNameInlowerCase);
  }

  /**
   * Add procedure into this catalog. Names are case insensitive.
   *
   * @param procedure
   */
  public void addProcedure(Procedure procedure) {
    String nameInLowerCase = Ascii.toLowerCase(procedure.getName());
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(!procedures.containsKey(nameInLowerCase));
    procedures.put(nameInLowerCase, procedure);
  }

  /** Removes the passed procedure from this catalog. */
  public void removeProcedure(Procedure procedure) {
    removeProcedure(procedure.getName());
  }

  /** Removes the procedure with the passed name from this catalog. Names are case insensitive. */
  public void removeProcedure(String name) {
    String nameInLowerCase = Ascii.toLowerCase(name);
    Preconditions.checkState(!registered);
    Preconditions.checkArgument(procedures.containsKey(nameInLowerCase));
    procedures.remove(nameInLowerCase);
  }

  public void setDescriptorPool(DescriptorPool descriptorPool) {
    Preconditions.checkState(!registered);
    Preconditions.checkState(this.descriptorPool == null);
    this.descriptorPool = descriptorPool;
  }

  @Override
  public String getFullName() {
    return name;
  }

  public TypeFactory getTypeFactory() {
    return typeFactory;
  }

  public ImmutableList<String> getTableNameList() {
    return ImmutableList.copyOf(tables.keySet());
  }

  public ImmutableList<SimpleTable> getTableList() {
    return ImmutableList.copyOf(tables.values());
  }

  public ImmutableList<String> getPropertyGraphNameList() {
    return ImmutableList.copyOf(propertyGraphs.keySet());
  }

  public ImmutableList<PropertyGraph> getPropertyGraphList() {
    return ImmutableList.copyOf(propertyGraphs.values());
  }

  public ImmutableList<Type> getTypeList() {
    return ImmutableList.copyOf(types.values());
  }

  public ImmutableList<SimpleCatalog> getCatalogList() {
    return ImmutableList.copyOf(catalogs.values());
  }

  public ImmutableList<Function> getFunctionList() {
    return ImmutableList.copyOf(functionsByFullName.values());
  }

  public ImmutableList<TableValuedFunction> getTVFList() {
    return ImmutableList.copyOf(customTvfs.values());
  }

  public ImmutableList<String> getFunctionNameList() {
    return ImmutableList.copyOf(functionsByFullName.keySet());
  }

  public ImmutableList<String> getTVFNameList() {
    return ImmutableList.copyOf(customTvfs.keySet());
  }

  public ImmutableList<Procedure> getProcedureList() {
    return ImmutableList.copyOf(procedures.values());
  }

  public ImmutableList<Connection> getConnectionList() {
    return ImmutableList.copyOf(connections.values());
  }

  @Override
  protected Connection getConnection(String name, FindOptions options) {
    return connections.get(Ascii.toLowerCase(name));
  }

  public ImmutableList<Constant> getConstantList() {
    return ImmutableList.copyOf(constants.values());
  }

  @Override
  protected Constant getConstant(String name, FindOptions options) {
    return constants.get(Ascii.toLowerCase(name));
  }

  public ImmutableList<Model> getModelList() {
    return ImmutableList.copyOf(models.values());
  }

  @Override
  protected Model getModel(String name, FindOptions options) {
    return models.get(Ascii.toLowerCase(name));
  }

  @Override
  public SimpleTable getTable(String name, FindOptions options) {
    return tables.get(Ascii.toLowerCase(name));
  }

  @Override
  @Nullable
  public Type getType(String name, FindOptions options) {
    if (types.containsKey(Ascii.toLowerCase(name))) {
      return types.get(Ascii.toLowerCase(name));
    }
    if (descriptorPool != null) {
      GoogleSQLDescriptor descriptor = descriptorPool.findMessageTypeByName(name);
      if (descriptor != null) {
        return getTypeFactory().createProtoType(descriptor);
      }
      GoogleSQLEnumDescriptor enumDescriptor = descriptorPool.findEnumTypeByName(name);
      if (enumDescriptor != null) {
        return getTypeFactory().createEnumType(enumDescriptor);
      }
    }
    return null;
  }

  @Override
  public SimpleCatalog getCatalog(String name, FindOptions options) {
    return catalogs.get(Ascii.toLowerCase(name));
  }

  @Override
  public SimpleCatalog getCatalog(String name) {
    return getCatalog(name, new FindOptions());
  }

  @Override
  protected Function getFunction(String name, FindOptions options) {
    return customFunctions.get(Ascii.toLowerCase(name));
  }

  public Function getFunctionByFullName(String fullName) {
    Function function = functionsByFullName.get(Ascii.toLowerCase(fullName));
    if (function == null) {
      for (SimpleCatalog catalog : catalogs.values()) {
        function = catalog.getFunctionByFullName(fullName);
        if (function != null) {
          break;
        }
      }
    }
    return function;
  }

  @Override
  protected TableValuedFunction getTableValuedFunction(String name, FindOptions options) {
    return customTvfs.get(Ascii.toLowerCase(name));
  }

  public TableValuedFunction getTvfByFullName(String fullName) {
    return tvfsByFullName.get(Ascii.toLowerCase(fullName));
  }

  @Override
  public PropertyGraph getPropertyGraph(String name, FindOptions options) {
    return propertyGraphs.get(Ascii.toLowerCase(name));
  }

  public SimpleTable getTableById(long serializationId) {
    SimpleTable table = tablesById.get(serializationId);
    if (table == null) {
      for (SimpleCatalog catalog : catalogs.values()) {
        table = catalog.getTableById(serializationId);
        if (table != null) {
          break;
        }
      }
    }
    return table;
  }

  public SimpleModel getModelById(long serializationId) {
    SimpleModel model = modelsById.get(serializationId);
    if (model == null) {
      for (SimpleCatalog catalog : catalogs.values()) {
        model = catalog.getModelById(serializationId);
        if (model != null) {
          break;
        }
      }
    }
    return model;
  }

  public Connection getConnectionByFullName(String fullName) {
    return getConnection(fullName);
  }

  public Sequence getSequenceByFullName(String fullName) {
    return sequences.get(Ascii.toLowerCase(fullName));
  }

  @Override
  protected Procedure getProcedure(String name, FindOptions options) {
    return procedures.get(Ascii.toLowerCase(name));
  }

  /**
   * AutoCloseable implementation that unregisters the SimpleCatalog automatically when used in
   * try-with-resources.
   */
  public class AutoUnregister implements AutoCloseable {
    @Override
    public void close() {
      unregister();
    }
  }

  public static SimpleCatalog deserialize(
      SimpleCatalogProto proto, ImmutableList<? extends DescriptorPool> pools) {
    SimpleCatalog catalog = new SimpleCatalog(proto.getName());

    for (SimpleTableProto tableProto : proto.getTableList()) {
      String name =
          tableProto.hasNameInCatalog() ? tableProto.getNameInCatalog() : tableProto.getName();
      catalog.addSimpleTable(
          name, SimpleTable.deserialize(tableProto, pools, catalog.getTypeFactory()));
    }

    for (NamedTypeProto typeProto : proto.getNamedTypeList()) {
      catalog.addType(
          typeProto.getName(), catalog.getTypeFactory().deserialize(typeProto.getType(), pools));
    }

    for (SimpleCatalogProto catalogProto : proto.getCatalogList()) {
      catalog.addSimpleCatalog(SimpleCatalog.deserialize(catalogProto, pools));
    }

    for (SimpleConnectionProto connectionProto : proto.getConnectionList()) {
      catalog.addConnection(SimpleConnection.deserialize(connectionProto));
    }

    for (SimpleConstantProto constantProto : proto.getConstantList()) {
      catalog.addConstant(Constant.deserialize(constantProto, pools, catalog.getTypeFactory()));
    }

    for (SimpleModelProto modelProto : proto.getModelList()) {
      catalog.addModel(SimpleModel.deserialize(modelProto, pools, catalog.getTypeFactory()));
    }

    for (TableValuedFunctionProto tvfProto : proto.getCustomTvfList()) {
      catalog.addTableValuedFunction(
          TableValuedFunction.deserialize(tvfProto, pools, catalog.getTypeFactory()));
    }

    for (FunctionProto functionProto : proto.getCustomFunctionList()) {
      catalog.addFunction(Function.deserialize(functionProto, pools));
    }

    for (ProcedureProto procedureProto : proto.getProcedureList()) {
      catalog.addProcedure(Procedure.deserialize(procedureProto, pools));
    }

    for (SimplePropertyGraphProto propertyGraphProto : proto.getPropertyGraphList()) {
      catalog.addSimplePropertyGraph(
          SimplePropertyGraph.deserialize(propertyGraphProto, pools, catalog));
    }

    for (SimpleSequenceProto sequenceProto : proto.getSequenceList()) {
      catalog.addSequence(SimpleSequence.deserialize(sequenceProto));
    }

    if (proto.hasBuiltinFunctionOptions()) {
      GoogleSQLBuiltinFunctionOptions options =
          new GoogleSQLBuiltinFunctionOptions(proto.getBuiltinFunctionOptions());
      catalog.addGoogleSQLFunctionsAndTypes(options);
    }

    if (proto.hasFileDescriptorSetIndex()) {
      catalog.setDescriptorPool(pools.get(proto.getFileDescriptorSetIndex()));
    }

    return catalog;
  }
}
