

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

# Geography functions

GoogleSQL supports geography functions.
Geography functions operate on or generate GoogleSQL
`GEOGRAPHY` values. The signature of most geography
functions starts with `ST_`. GoogleSQL supports the following functions
that can be used to analyze geographical data, determine spatial relationships
between geographical features, and construct or manipulate
`GEOGRAPHY`s.

All GoogleSQL geography functions return `NULL` if any input argument
is `NULL`.

## Categories

The geography functions are grouped into the following categories based on their
behavior:

<!-- disableFinding(LINK_ID) -->
<table>
  <thead>
    <tr>
      <td>Category</td>
      <td width='300px'>Functions</td>
      <td>Description</td>
    </tr>
  </thead>
  <tbody>
    <tr>
      <td>Constructors</td>
      <td>
        <a href="#st_geogpoint"><code>ST_GEOGPOINT</code></a><br>
        <a href="#st_makeline"><code>ST_MAKELINE</code></a><br>
        <a href="#st_makepolygon"><code>ST_MAKEPOLYGON</code></a><br>
        <a href="#st_makepolygonoriented"><code>ST_MAKEPOLYGONORIENTED</code></a>
      </td>
      <td>
        Functions that build new
        geography values from coordinates
        or existing geographies.
      </td>
    </tr>
    <tr>
      <td>Parsers</td>
      <td>
        <a href="#st_geogfrom"><code>ST_GEOGFROM</code></a><br>
        <a href="#st_geogfromgeojson"><code>ST_GEOGFROMGEOJSON</code></a><br>
        <a href="#st_geogfromkml"><code>ST_GEOGFROMKML</code></a><br>
        <a href="#st_geogfromtext"><code>ST_GEOGFROMTEXT</code></a><br>
        <a href="#st_geogfromwkb"><code>ST_GEOGFROMWKB</code></a><br>
        <a href="#st_geogpointfromgeohash"><code>ST_GEOGPOINTFROMGEOHASH</code></a><br>
      </td>
      <td>
        Functions that create geographies
        from an external format such as
        <a href="https://en.wikipedia.org/wiki/Well-known_text">WKT</a> and
        <a href="https://en.wikipedia.org/wiki/GeoJSON">GeoJSON</a>.
      </td>
    </tr>
    <tr>
      <td>Formatters</td>
      <td>
        <a href="#st_asbinary"><code>ST_ASBINARY</code></a><br>
        <a href="#st_asgeojson"><code>ST_ASGEOJSON</code></a><br>
        <a href="#st_askml"><code>ST_ASKML</code></a><br>
        <a href="#st_astext"><code>ST_ASTEXT</code></a><br>
        <a href="#st_geohash"><code>ST_GEOHASH</code></a>
      </td>
      <td>
        Functions that export geographies
        to an external format such as WKT.
      </td>
    </tr>
    <tr>
      <td>Transformations</td>
      <td>
        
        <a href="#st_boundary"><code>ST_BOUNDARY</code></a><br>
        <a href="#st_buffer"><code>ST_BUFFER</code></a><br>
        <a href="#st_bufferwithtolerance"><code>ST_BUFFERWITHTOLERANCE</code></a><br>
        <a href="#st_centroid"><code>ST_CENTROID</code></a><br>
        <a href="#st_centroid_agg"><code>ST_CENTROID_AGG</code></a> (Aggregate)<br>
        <a href="#st_closestpoint"><code>ST_CLOSESTPOINT</code></a><br>
        <a href="#st_convexhull"><code>ST_CONVEXHULL</code></a><br>
        <a href="#st_difference"><code>ST_DIFFERENCE</code></a><br>
        <a href="#st_exteriorring"><code>ST_EXTERIORRING</code></a><br>
        <a href="#st_interiorrings"><code>ST_INTERIORRINGS</code></a><br>
        <a href="#st_intersection"><code>ST_INTERSECTION</code></a><br>
        <a href="#st_lineinterpolatepoint"><code>ST_LINEINTERPOLATEPOINT</code></a><br>
        <a href="#st_linesubstring"><code>ST_LINESUBSTRING</code></a><br>
        <a href="#st_simplify"><code>ST_SIMPLIFY</code></a><br>
        <a href="#st_snaptogrid"><code>ST_SNAPTOGRID</code></a><br>
        <a href="#st_union"><code>ST_UNION</code></a><br>
        <a href="#st_union_agg"><code>ST_UNION_AGG</code></a> (Aggregate)<br>
      </td>
      <td>
        Functions that generate a new
        geography based on input.
      </td>
    </tr>
    <tr>
      <td>Accessors</td>
      <td>
        <a href="#st_dimension"><code>ST_DIMENSION</code></a><br>
        <a href="#st_dump"><code>ST_DUMP</code></a><br>
        <a href="#st_dumppoints"><code>ST_DUMPPOINTS</code></a><br>
        <a href="#st_endpoint"><code>ST_ENDPOINT</code></a><br>
        <a href="#st_geometrytype"><code>ST_GEOMETRYTYPE</code></a><br>
        <a href="#st_isclosed"><code>ST_ISCLOSED</code></a><br>
        <a href="#st_iscollection"><code>ST_ISCOLLECTION</code></a><br>
        <a href="#st_isempty"><code>ST_ISEMPTY</code></a><br>
        <a href="#st_isring"><code>ST_ISRING</code></a><br>
        <a href="#st_npoints"><code>ST_NPOINTS</code></a><br>
        <a href="#st_numgeometries"><code>ST_NUMGEOMETRIES</code></a><br>
        <a href="#st_numpoints"><code>ST_NUMPOINTS</code></a><br>
        <a href="#st_pointn"><code>ST_POINTN</code></a><br>
        <a href="#st_startpoint"><code>ST_STARTPOINT</code></a><br>
        <a href="#st_x"><code>ST_X</code></a><br>
        <a href="#st_y"><code>ST_Y</code></a><br>
      </td>
      <td>
        Functions that provide access to
        properties of a geography without
        side-effects.
      </td>
    </tr>
    <tr>
      <td>Predicates</td>
      <td>
        <a href="#st_contains"><code>ST_CONTAINS</code></a><br>
        <a href="#st_coveredby"><code>ST_COVEREDBY</code></a><br>
        <a href="#st_covers"><code>ST_COVERS</code></a><br>
        <a href="#st_disjoint"><code>ST_DISJOINT</code></a><br>
        <a href="#st_dwithin"><code>ST_DWITHIN</code></a><br>
        <a href="#st_equals"><code>ST_EQUALS</code></a><br>
        <a href="#st_hausdorffdwithin"><code>ST_HAUSDORFFDWITHIN</code></a><br>
        <a href="#st_intersects"><code>ST_INTERSECTS</code></a><br>
        <a href="#st_intersectsbox"><code>ST_INTERSECTSBOX</code></a><br>
        <a href="#st_touches"><code>ST_TOUCHES</code></a><br>
        <a href="#st_within"><code>ST_WITHIN</code></a><br>
      </td>
      <td>
        Functions that return <code>TRUE</code> or
        <code>FALSE</code> for some spatial
        relationship between two
        geographies or some property of
        a geography. These functions
        are commonly used in filter
        clauses.
      </td>
    </tr>
    <tr>
      <td>Measures</td>
      <td>
        <a href="#st_angle"><code>ST_ANGLE</code></a><br>
        <a href="#st_area"><code>ST_AREA</code></a><br>
        <a href="#st_azimuth"><code>ST_AZIMUTH</code></a><br>
        <a href="#st_boundingbox"><code>ST_BOUNDINGBOX</code></a><br>
        <a href="#st_distance"><code>ST_DISTANCE</code></a><br>
        <a href="#st_extent"><code>ST_EXTENT</code></a> (Aggregate)<br>
        <a href="#st_hausdorffdistance"><code>ST_HAUSDORFFDISTANCE</code></a><br>
        <a href="#st_linelocatepoint"><code>ST_LINELOCATEPOINT</code></a><br>
        <a href="#st_length"><code>ST_LENGTH</code></a><br>
        <a href="#st_maxdistance"><code>ST_MAXDISTANCE</code></a><br>
        <a href="#st_perimeter"><code>ST_PERIMETER</code></a><br>
      </td>
      <td>
        Functions that compute measurements
        of one or more geographies.
      </td>
    </tr>
    
    <tr>
      <td>Clustering</td>
      <td>
        <a href="#st_clusterdbscan"><code>ST_CLUSTERDBSCAN</code></a>
      </td>
      <td>
        Functions that perform clustering on geographies.
      </td>
    </tr>
    
    
    <tr>
      <td>S2 functions</td>
      <td>
        <a href="#s2_cellidfrompoint"><code>S2_CELLIDFROMPOINT</code></a><br>
        <a href="#s2_coveringcellids"><code>S2_COVERINGCELLIDS</code></a><br>
      </td>
      <td>
        Functions for working with S2 cell coverings of GEOGRAPHY.
      </td>
    </tr>
    
  </tbody>
</table>
<!-- enableFinding(LINK_ID) -->

## Function list

<table>
  <thead>
    <tr>
      <th>Name</th>
      <th>Summary</th>
    </tr>
  </thead>
  <tbody>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#s2_cellidfrompoint"><code>S2_CELLIDFROMPOINT</code></a>
</td>
  <td>
    Gets the S2 cell ID covering a point <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#s2_coveringcellids"><code>S2_COVERINGCELLIDS</code></a>
</td>
  <td>
    Gets an array of S2 cell IDs that cover a <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_angle"><code>ST_ANGLE</code></a>
</td>
  <td>
    Takes three point <code>GEOGRAPHY</code> values, which represent two
    intersecting lines, and returns the angle between these lines.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_area"><code>ST_AREA</code></a>
</td>
  <td>
    Gets the area covered by the polygons in a <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_asbinary"><code>ST_ASBINARY</code></a>
</td>
  <td>
    Converts a <code>GEOGRAPHY</code> value to a
    <code>BYTES</code> WKB geography value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_asgeojson"><code>ST_ASGEOJSON</code></a>
</td>
  <td>
    Converts a <code>GEOGRAPHY</code> value to a <code>STRING</code>
    GeoJSON geography value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_askml"><code>ST_ASKML</code></a>
</td>
  <td>
    Converts a <code>GEOGRAPHY</code> value to a <code>STRING</code>
    KML geometry value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_astext"><code>ST_ASTEXT</code></a>
</td>
  <td>
    Converts a <code>GEOGRAPHY</code> value to a
    <code>STRING</code> WKT geography value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_azimuth"><code>ST_AZIMUTH</code></a>
</td>
  <td>
    Gets the azimuth of a line segment formed by two
    point <code>GEOGRAPHY</code> values.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_boundary"><code>ST_BOUNDARY</code></a>
</td>
  <td>
    Gets the union of component boundaries in a
    <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_boundingbox"><code>ST_BOUNDINGBOX</code></a>
</td>
  <td>
    Gets the bounding box for a <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_buffer"><code>ST_BUFFER</code></a>
</td>
  <td>
    Gets the buffer around a <code>GEOGRAPHY</code> value, using a specific
    number of segments.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_bufferwithtolerance"><code>ST_BUFFERWITHTOLERANCE</code></a>
</td>
  <td>
    Gets the buffer around a <code>GEOGRAPHY</code> value, using tolerance.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_centroid"><code>ST_CENTROID</code></a>
</td>
  <td>
    Gets the centroid of a <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_closestpoint"><code>ST_CLOSESTPOINT</code></a>
</td>
  <td>
    Gets the point on a <code>GEOGRAPHY</code> value which is closest to any
    point in a second <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_clusterdbscan"><code>ST_CLUSTERDBSCAN</code></a>
</td>
  <td>
    Performs DBSCAN clustering on a group of <code>GEOGRAPHY</code> values and
    produces a 0-based cluster number for this row.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_contains"><code>ST_CONTAINS</code></a>
</td>
  <td>
    Checks if one <code>GEOGRAPHY</code> value contains another
    <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_convexhull"><code>ST_CONVEXHULL</code></a>
</td>
  <td>
    Returns the convex hull for a <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_coveredby"><code>ST_COVEREDBY</code></a>
</td>
  <td>
    Checks if all points of a <code>GEOGRAPHY</code> value are on the boundary
    or interior of another <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_covers"><code>ST_COVERS</code></a>
</td>
  <td>
    Checks if all points of a <code>GEOGRAPHY</code> value are on the boundary
    or interior of another <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_difference"><code>ST_DIFFERENCE</code></a>
</td>
  <td>
    Gets the point set difference between two <code>GEOGRAPHY</code> values.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_dimension"><code>ST_DIMENSION</code></a>
</td>
  <td>
    Gets the dimension of the highest-dimensional element in a
    <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_disjoint"><code>ST_DISJOINT</code></a>
</td>
  <td>
    Checks if two <code>GEOGRAPHY</code> values are disjoint (don't intersect).
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_distance"><code>ST_DISTANCE</code></a>
</td>
  <td>
    Gets the shortest distance in meters between two <code>GEOGRAPHY</code>
    values.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_dump"><code>ST_DUMP</code></a>
</td>
  <td>
    Returns an array of simple <code>GEOGRAPHY</code> components in a
    <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_dumppoints"><code>ST_DUMPPOINTS</code></a>
</td>
  <td>
    Produces an array of <code>GEOGRAPHY</code> points with all points, line
    vertices, and polygon vertices in a <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_dwithin"><code>ST_DWITHIN</code></a>
</td>
  <td>
    Checks if any points in two <code>GEOGRAPHY</code> values are within a given
    distance.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_endpoint"><code>ST_ENDPOINT</code></a>
</td>
  <td>
    Gets the last point of a linestring <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_equals"><code>ST_EQUALS</code></a>
</td>
  <td>
    Checks if two <code>GEOGRAPHY</code> values represent the same
    <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_extent"><code>ST_EXTENT</code></a>
</td>
  <td>
    Gets the bounding box for a group of <code>GEOGRAPHY</code> values.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_exteriorring"><code>ST_EXTERIORRING</code></a>
</td>
  <td>
    Returns a linestring <code>GEOGRAPHY</code> value that corresponds to the
    outermost ring of a polygon <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_geogfrom"><code>ST_GEOGFROM</code></a>
</td>
  <td>
    Converts a <code>STRING</code> or <code>BYTES</code> value
    into a <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_geogfromgeojson"><code>ST_GEOGFROMGEOJSON</code></a>
</td>
  <td>
    Converts a <code>STRING</code> GeoJSON geometry value into a
    <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_geogfromkml"><code>ST_GEOGFROMKML</code></a>
</td>
  <td>
    Converts a <code>STRING</code> KML geometry value into a
    <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_geogfromtext"><code>ST_GEOGFROMTEXT</code></a>
</td>
  <td>
    Converts a <code>STRING</code> WKT geometry value into a
    <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_geogfromwkb"><code>ST_GEOGFROMWKB</code></a>
</td>
  <td>
    Converts a <code>BYTES</code> or hexadecimal-text <code>STRING</code> WKT
    geometry value into a <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_geogpoint"><code>ST_GEOGPOINT</code></a>
</td>
  <td>
    Creates a point <code>GEOGRAPHY</code> value for a given longitude and
    latitude.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_geogpointfromgeohash"><code>ST_GEOGPOINTFROMGEOHASH</code></a>
</td>
  <td>
    Gets a point <code>GEOGRAPHY</code> value that's in the middle of a
    bounding box defined in a <code>STRING</code> GeoHash value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_geohash"><code>ST_GEOHASH</code></a>
</td>
  <td>
    Converts a point <code>GEOGRAPHY</code> value to a <code>STRING</code>
    GeoHash value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_geometrytype"><code>ST_GEOMETRYTYPE</code></a>
</td>
  <td>
    Gets the Open Geospatial Consortium (OGC) geometry type for a
    <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_hausdorffdistance"><code>ST_HAUSDORFFDISTANCE</code></a>
</td>
  <td>Gets the discrete Hausdorff distance between two geometries.</td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_hausdorffdwithin"><code>ST_HAUSDORFFDWITHIN</code></a>
</td>
  <td>
    Checks if the Hausdorff distance between two <code>GEOGRAPHY</code> values
    is within a given distance.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_interiorrings"><code>ST_INTERIORRINGS</code></a>
</td>
  <td>
    Gets the interior rings of a polygon <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_intersection"><code>ST_INTERSECTION</code></a>
</td>
  <td>
    Gets the point set intersection of two <code>GEOGRAPHY</code> values.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_intersects"><code>ST_INTERSECTS</code></a>
</td>
  <td>
    Checks if at least one point appears in two <code>GEOGRAPHY</code>
    values.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_intersectsbox"><code>ST_INTERSECTSBOX</code></a>
</td>
  <td>
    Checks if a <code>GEOGRAPHY</code> value intersects a rectangle.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_isclosed"><code>ST_ISCLOSED</code></a>
</td>
  <td>
    Checks if all components in a <code>GEOGRAPHY</code> value are closed.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_iscollection"><code>ST_ISCOLLECTION</code></a>
</td>
  <td>
    Checks if the total number of points, linestrings, and polygons is
    greater than one in a <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_isempty"><code>ST_ISEMPTY</code></a>
</td>
  <td>
    Checks if a <code>GEOGRAPHY</code> value is empty.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_isring"><code>ST_ISRING</code></a>
</td>
  <td>
    Checks if a <code>GEOGRAPHY</code> value is a closed, simple
    linestring.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_length"><code>ST_LENGTH</code></a>
</td>
  <td>
    Gets the total length of lines in a <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_lineinterpolatepoint"><code>ST_LINEINTERPOLATEPOINT</code></a>
</td>
  <td>
    Gets a point at a specific fraction in a linestring <code>GEOGRAPHY</code>
    value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_linelocatepoint"><code>ST_LINELOCATEPOINT</code></a>
</td>
  <td>
    Gets a section of a linestring <code>GEOGRAPHY</code> value between the
    start point and a point <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_linesubstring"><code>ST_LINESUBSTRING</code></a>
</td>
  <td>
    Gets a segment of a single linestring at a specific starting and
    ending fraction.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_makeline"><code>ST_MAKELINE</code></a>
</td>
  <td>
    Creates a linestring <code>GEOGRAPHY</code> value by concatenating the point
    and linestring vertices of <code>GEOGRAPHY</code> values.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_makepolygon"><code>ST_MAKEPOLYGON</code></a>
</td>
  <td>
    Constructs a polygon <code>GEOGRAPHY</code> value by combining
    a polygon shell with polygon holes.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_makepolygonoriented"><code>ST_MAKEPOLYGONORIENTED</code></a>
</td>
  <td>
    Constructs a polygon <code>GEOGRAPHY</code> value, using an array of
    linestring <code>GEOGRAPHY</code> values. The vertex ordering of each
    linestring determines the orientation of each polygon ring.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_maxdistance"><code>ST_MAXDISTANCE</code></a>
</td>
  <td>
    Gets the longest distance between two non-empty
    <code>GEOGRAPHY</code> values.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_npoints"><code>ST_NPOINTS</code></a>
</td>
  <td>
    An alias of <code>ST_NUMPOINTS</code>.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_numgeometries"><code>ST_NUMGEOMETRIES</code></a>
</td>
  <td>
    Gets the number of geometries in a <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_numpoints"><code>ST_NUMPOINTS</code></a>
</td>
  <td>
    Gets the number of vertices in the a <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_perimeter"><code>ST_PERIMETER</code></a>
</td>
  <td>
    Gets the length of the boundary of the polygons in a
    <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_pointn"><code>ST_POINTN</code></a>
</td>
  <td>
    Gets the point at a specific index of a linestring <code>GEOGRAPHY</code>
    value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_simplify"><code>ST_SIMPLIFY</code></a>
</td>
  <td>
    Converts a <code>GEOGRAPHY</code> value into a simplified
    <code>GEOGRAPHY</code> value, using tolerance.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_snaptogrid"><code>ST_SNAPTOGRID</code></a>
</td>
  <td>
    Produces a <code>GEOGRAPHY</code> value, where each vertex has
    been snapped to a longitude/latitude grid.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_startpoint"><code>ST_STARTPOINT</code></a>
</td>
  <td>
    Gets the first point of a linestring <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_touches"><code>ST_TOUCHES</code></a>
</td>
  <td>
    Checks if two <code>GEOGRAPHY</code> values intersect and their interiors
    have no elements in common.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_union"><code>ST_UNION</code></a>
</td>
  <td>
    Gets the point set union of multiple <code>GEOGRAPHY</code> values.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_union_agg"><code>ST_UNION_AGG</code></a>
</td>
  <td>
    Aggregates over <code>GEOGRAPHY</code> values and gets their
    point set union.
    
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_within"><code>ST_WITHIN</code></a>
</td>
  <td>
    Checks if one <code>GEOGRAPHY</code> value contains another
    <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_x"><code>ST_X</code></a>
</td>
  <td>
    Gets the longitude from a point <code>GEOGRAPHY</code> value.
  </td>
</tr>

<tr>
  <td><a href="https://github.com/google/googlesql/blob/master/docs/geography_functions.md#st_y"><code>ST_Y</code></a>
</td>
  <td>
    Gets the latitude from a point <code>GEOGRAPHY</code> value.
  </td>
</tr>

  </tbody>
</table>

## `S2_CELLIDFROMPOINT`

```googlesql
S2_CELLIDFROMPOINT(point_geography[, level => cell_level])
```

**Description**

Returns the [S2 cell ID][s2-cells-link] covering a point `GEOGRAPHY`.

+ The optional `INT64` parameter `level` specifies the S2 cell level for the
  returned cell. Naming this argument is optional.

This is advanced functionality for interoperability with systems utilizing the
[S2 Geometry Library][s2-root-link].

**Constraints**

+ Returns the cell ID as a signed `INT64` bit-equivalent to
  [unsigned 64-bit integer representation][s2-cells-link].
+ Can return negative cell IDs.
+ Valid S2 cell levels are 0 to 30.
+ `level` defaults to 30 if not explicitly specified.
+ The function only supports a single point GEOGRAPHY. Use the `SAFE` prefix if
  the input can be multipoint, linestring, polygon, or an empty `GEOGRAPHY`.
+ To compute the covering of a complex `GEOGRAPHY`, use
  [S2_COVERINGCELLIDS][s2-coveringcellids].

**Return type**

`INT64`

**Example**

```googlesql
WITH data AS (
  SELECT 1 AS id, ST_GEOGPOINT(-122, 47) AS geo
  UNION ALL
  -- empty geography isn't supported
  SELECT 2 AS id, ST_GEOGFROMTEXT('POINT EMPTY') AS geo
  UNION ALL
  -- only points are supported
  SELECT 3 AS id, ST_GEOGFROMTEXT('LINESTRING(1 2, 3 4)') AS geo
)
SELECT id,
       SAFE.S2_CELLIDFROMPOINT(geo) cell30,
       SAFE.S2_CELLIDFROMPOINT(geo, level => 10) cell10
FROM data;

/*----+---------------------+---------------------+
 | id | cell30              | cell10              |
 +----+---------------------+---------------------+
 | 1  | 6093613931972369317 | 6093613287902019584 |
 | 2  | NULL                | NULL                |
 | 3  | NULL                | NULL                |
 +----+---------------------+---------------------*/
```

[s2-cells-link]: https://s2geometry.io/devguide/s2cell_hierarchy

[s2-root-link]: https://s2geometry.io/

[s2-coveringcellids]: #s2_coveringcellids

## `S2_COVERINGCELLIDS`

```googlesql
S2_COVERINGCELLIDS(
    geography
    [, min_level => cell_level]
    [, max_level => cell_level]
    [, max_cells => max_cells]
    [, buffer => buffer])
```

**Description**

Returns an array of [S2 cell IDs][s2-cells-link] that cover the input
`GEOGRAPHY`. The function returns at most `max_cells` cells. The optional
arguments `min_level` and `max_level` specify minimum and maximum levels for
returned S2 cells. The array size is limited by the optional `max_cells`
argument. The optional `buffer` argument specifies a buffering factor in
meters; the region being covered is expanded from the extent of the
input geography by this amount.

This is advanced functionality for interoperability with systems utilizing the
[S2 Geometry Library][s2-root-link].

**Constraints**

+ Returns the cell ID as a signed `INT64` bit-equivalent to
  [unsigned 64-bit integer representation][s2-cells-link].
+ Can return negative cell IDs.
+ Valid S2 cell levels are 0 to 30.
+ `max_cells` defaults to 8 if not explicitly specified.
+ `buffer` should be nonnegative. It defaults to 0.0 meters if not explicitly
  specified.

**Return type**

`ARRAY<INT64>`

**Example**

```googlesql
WITH data AS (
  SELECT 1 AS id, ST_GEOGPOINT(-122, 47) AS geo
  UNION ALL
  SELECT 2 AS id, ST_GEOGFROMTEXT('POINT EMPTY') AS geo
  UNION ALL
  SELECT 3 AS id, ST_GEOGFROMTEXT('LINESTRING(-122.12 47.67, -122.19 47.69)') AS geo
)
SELECT id, S2_COVERINGCELLIDS(geo, min_level => 12) cells
FROM data;

/*----+--------------------------------------------------------------------------------------+
 | id | cells                                                                                |
 +----+--------------------------------------------------------------------------------------+
 | 1  | [6093613931972369317]                                                                |
 | 2  | []                                                                                   |
 | 3  | [6093384954555662336, 6093390709811838976, 6093390735581642752, 6093390740145045504, |
 |    |  6093390791416217600, 6093390812891054080, 6093390817187069952, 6093496378892222464] |
 +----+--------------------------------------------------------------------------------------*/
```

[s2-cells-link]: https://s2geometry.io/devguide/s2cell_hierarchy

[s2-root-link]: https://s2geometry.io/

## `ST_ANGLE`

```googlesql
ST_ANGLE(point_geography_1, point_geography_2, point_geography_3)
```

**Description**

Takes three point `GEOGRAPHY` values, which represent two intersecting lines.
Returns the angle between these lines. Point 2 and point 1 represent the first
line and point 2 and point 3 represent the second line. The angle between
these lines is in radians, in the range `[0, 2pi)`. The angle is measured
clockwise from the first line to the second line.

`ST_ANGLE` has the following edge cases:

+ If points 2 and 3 are the same, returns `NULL`.
+ If points 2 and 1 are the same, returns `NULL`.
+ If points 2 and 3 are exactly antipodal, returns `NULL`.
+ If points 2 and 1 are exactly antipodal, returns `NULL`.
+ If any of the input geographies aren't single points or are the empty
  geography, then throws an error.

**Return type**

`DOUBLE`

**Example**

```googlesql
WITH geos AS (
  SELECT 1 id, ST_GEOGPOINT(1, 0) geo1, ST_GEOGPOINT(0, 0) geo2, ST_GEOGPOINT(0, 1) geo3 UNION ALL
  SELECT 2 id, ST_GEOGPOINT(0, 0), ST_GEOGPOINT(1, 0), ST_GEOGPOINT(0, 1) UNION ALL
  SELECT 3 id, ST_GEOGPOINT(1, 0), ST_GEOGPOINT(0, 0), ST_GEOGPOINT(1, 0) UNION ALL
  SELECT 4 id, ST_GEOGPOINT(1, 0) geo1, ST_GEOGPOINT(0, 0) geo2, ST_GEOGPOINT(0, 0) geo3 UNION ALL
  SELECT 5 id, ST_GEOGPOINT(0, 0), ST_GEOGPOINT(-30, 0), ST_GEOGPOINT(150, 0) UNION ALL
  SELECT 6 id, ST_GEOGPOINT(0, 0), NULL, NULL UNION ALL
  SELECT 7 id, NULL, ST_GEOGPOINT(0, 0), NULL UNION ALL
  SELECT 8 id, NULL, NULL, ST_GEOGPOINT(0, 0))
SELECT ST_ANGLE(geo1,geo2,geo3) AS angle FROM geos ORDER BY id;

/*---------------------+
 | angle               |
 +---------------------+
 | 4.71238898038469    |
 | 0.78547432161873854 |
 | 0                   |
 | NULL                |
 | NULL                |
 | NULL                |
 | NULL                |
 | NULL                |
 +---------------------*/
```

## `ST_AREA`

```googlesql
ST_AREA(geography_expression[, use_spheroid])
```

**Description**

Returns the area in square meters covered by the polygons in the input
`GEOGRAPHY`.

If `geography_expression` is a point or a line, returns zero. If
`geography_expression` is a collection, returns the area of the polygons in the
collection; if the collection doesn't contain polygons, returns zero.

The optional `use_spheroid` parameter determines how this function measures
distance. If `use_spheroid` is `FALSE`, the function measures distance on the
surface of a perfect sphere.

The `use_spheroid` parameter currently only supports
the value `FALSE`. The default value of `use_spheroid` is `FALSE`.

**Return type**

`DOUBLE`

[wgs84-link]: https://en.wikipedia.org/wiki/World_Geodetic_System

## `ST_ASBINARY`

```googlesql
ST_ASBINARY(geography_expression)
```

**Description**

Returns the [WKB][wkb-link] representation of an input
`GEOGRAPHY`.

See [`ST_GEOGFROMWKB`][st-geogfromwkb] to construct a
`GEOGRAPHY` from WKB.

**Return type**

`BYTES`

[wkb-link]: https://en.wikipedia.org/wiki/Well-known_text#Well-known_binary

[st-geogfromwkb]: #st_geogfromwkb

## `ST_ASGEOJSON`

```googlesql
ST_ASGEOJSON(geography_expression)
```

**Description**

Returns the [RFC 7946][GeoJSON-spec-link] compliant [GeoJSON][geojson-link]
representation of the input `GEOGRAPHY`.

A GoogleSQL `GEOGRAPHY` has spherical
geodesic edges, whereas a GeoJSON `Geometry` object explicitly has planar edges.
To convert between these two types of edges, GoogleSQL adds additional
points to the line where necessary so that the resulting sequence of edges
remains within 10 meters of the original edge.

See [`ST_GEOGFROMGEOJSON`][st-geogfromgeojson] to construct a
`GEOGRAPHY` from GeoJSON.

**Return type**

`STRING`

[geojson-spec-link]: https://tools.ietf.org/html/rfc7946

[geojson-link]: https://en.wikipedia.org/wiki/GeoJSON

[st-geogfromgeojson]: #st_geogfromgeojson

## `ST_ASKML`

```googlesql
ST_ASKML(geography)
```

**Description**

Takes a `GEOGRAPHY` and returns a `STRING` [KML geometry][kml-geometry-link].
Coordinates are formatted with as few digits as possible without loss
of precision.

**Return type**

`STRING`

[kml-geometry-link]: https://developers.google.com/kml/documentation/kmlreference#geometry

## `ST_ASTEXT`

```googlesql
ST_ASTEXT(geography_expression)
```

**Description**

Returns the [WKT][wkt-link] representation of an input
`GEOGRAPHY`.

See [`ST_GEOGFROMTEXT`][st-geogfromtext] to construct a
`GEOGRAPHY` from WKT.

**Return type**

`STRING`

[wkt-link]: https://en.wikipedia.org/wiki/Well-known_text

[st-geogfromtext]: #st_geogfromtext

## `ST_AZIMUTH`

```googlesql
ST_AZIMUTH(point_geography_1, point_geography_2)
```

**Description**

Takes two point `GEOGRAPHY` values, and returns the azimuth of the line segment
formed by points 1 and 2. The azimuth is the angle in radians measured between
the line from point 1 facing true North to the line segment from point 1 to
point 2.

The positive angle is measured clockwise on the surface of a sphere. For
example, the azimuth for a line segment:

+   Pointing North is `0`
+   Pointing East is `PI/2`
+   Pointing South is `PI`
+   Pointing West is `3PI/2`

`ST_AZIMUTH` has the following edge cases:

+   If the two input points are the same, returns `NULL`.
+   If the two input points are exactly antipodal, returns `NULL`.
+   If either of the input geographies aren't single points or are the empty
    geography, throws an error.

**Return type**

`DOUBLE`

**Example**

```googlesql
WITH geos AS (
  SELECT 1 id, ST_GEOGPOINT(1, 0) AS geo1, ST_GEOGPOINT(0, 0) AS geo2 UNION ALL
  SELECT 2, ST_GEOGPOINT(0, 0), ST_GEOGPOINT(1, 0) UNION ALL
  SELECT 3, ST_GEOGPOINT(0, 0), ST_GEOGPOINT(0, 1) UNION ALL
  -- identical
  SELECT 4, ST_GEOGPOINT(0, 0), ST_GEOGPOINT(0, 0) UNION ALL
  -- antipode
  SELECT 5, ST_GEOGPOINT(-30, 0), ST_GEOGPOINT(150, 0) UNION ALL
  -- nulls
  SELECT 6, ST_GEOGPOINT(0, 0), NULL UNION ALL
  SELECT 7, NULL, ST_GEOGPOINT(0, 0))
SELECT ST_AZIMUTH(geo1, geo2) AS azimuth FROM geos ORDER BY id;

/*--------------------+
 | azimuth            |
 +--------------------+
 | 4.71238898038469   |
 | 1.5707963267948966 |
 | 0                  |
 | NULL               |
 | NULL               |
 | NULL               |
 | NULL               |
 +--------------------*/
```

## `ST_BOUNDARY`

```googlesql
ST_BOUNDARY(geography_expression)
```

**Description**

Returns a single `GEOGRAPHY` that contains the union
of the boundaries of each component in the given input
`GEOGRAPHY`.

The boundary of each component of a `GEOGRAPHY` is
defined as follows:

+   The boundary of a point is empty.
+   The boundary of a linestring consists of the endpoints of the linestring.
+   The boundary of a polygon consists of the linestrings that form the polygon
    shell and each of the polygon's holes.

**Return type**

`GEOGRAPHY`

## `ST_BOUNDINGBOX`

```googlesql
ST_BOUNDINGBOX(geography_expression)
```

**Description**

Returns a `STRUCT` that represents the bounding box for the specified geography.
The bounding box is the minimal rectangle that encloses the geography. The edges
of the rectangle follow constant lines of longitude and latitude.

Caveats:

+ Returns `NULL` if the input is `NULL` or an empty geography.
+ The bounding box might cross the antimeridian if this allows for a smaller
  rectangle. In this case, the bounding box has one of its longitudinal bounds
  outside of the [-180, 180] range, so that `xmin` is smaller than the eastmost
  value `xmax`.

**Return type**

`STRUCT<xmin DOUBLE, ymin DOUBLE, xmax DOUBLE, ymax DOUBLE>`.

Bounding box parts:

+ `xmin`: The westmost constant longitude line that bounds the rectangle.
+ `xmax`: The eastmost constant longitude line that bounds the rectangle.
+ `ymin`: The minimum constant latitude line that bounds the rectangle.
+ `ymax`: The maximum constant latitude line that bounds the rectangle.

**Example**

```googlesql
WITH data AS (
  SELECT 1 id, ST_GEOGFROMTEXT('POLYGON((-125 48, -124 46, -117 46, -117 49, -125 48))') g
  UNION ALL
  SELECT 2 id, ST_GEOGFROMTEXT('POLYGON((172 53, -130 55, -141 70, 172 53))') g
  UNION ALL
  SELECT 3 id, ST_GEOGFROMTEXT('POINT EMPTY') g
  UNION ALL
  SELECT 4 id, ST_GEOGFROMTEXT('POLYGON((172 53, -141 70, -130 55, 172 53))', oriented => TRUE)
)
SELECT id, ST_BOUNDINGBOX(g) AS box
FROM data

/*----+------------------------------------------+
 | id | box                                      |
 +----+------------------------------------------+
 | 1  | {xmin:-125, ymin:46, xmax:-117, ymax:49} |
 | 2  | {xmin:172, ymin:53, xmax:230, ymax:70}   |
 | 3  | NULL                                     |
 | 4  | {xmin:-180, ymin:-90, xmax:180, ymax:90} |
 +----+------------------------------------------*/
```

See [`ST_EXTENT`][st-extent] for the aggregate version of `ST_BOUNDINGBOX`.

[st-extent]: #st_extent

## `ST_BUFFER`

```googlesql
ST_BUFFER(
    geography,
    buffer_radius
    [, num_seg_quarter_circle => num_segments]
    [, use_spheroid => boolean_expression]
    [, endcap => endcap_style]
    [, side => line_side])
```

**Description**

Returns a `GEOGRAPHY` that represents the buffer around the input `GEOGRAPHY`.
This function is similar to [`ST_BUFFERWITHTOLERANCE`][st-bufferwithtolerance],
but you specify the number of segments instead of providing tolerance to
determine how much the resulting geography can deviate from the ideal
buffer radius.

+   `geography`: The input `GEOGRAPHY` to encircle with the buffer radius.
+   `buffer_radius`: `DOUBLE` that represents the radius of the
    buffer around the input geography. The radius is in meters. Note that
    polygons contract when buffered with a negative `buffer_radius`. Polygon
    shells and holes that are contracted to a point are discarded.
+   `num_seg_quarter_circle`: (Optional) `DOUBLE` specifies the
    number of segments that are used to approximate a quarter circle. The
    default value is `8.0`. Naming this argument is optional.
+   `endcap`: (Optional) `STRING` allows you to specify one of two endcap
    styles: `ROUND` and `FLAT`. The default value is `ROUND`. This option only
    affects the endcaps of buffered linestrings.
+   `side`: (Optional) `STRING` allows you to specify one of three possibilities
    for lines: `BOTH`, `LEFT`, and `RIGHT`. The default is `BOTH`. This option
    only affects how linestrings are buffered.
+   `use_spheroid`: (Optional) `BOOL` determines how this function measures
    distance. If `use_spheroid` is `FALSE`, the function measures distance on
    the surface of a perfect sphere. The `use_spheroid` parameter
    currently only supports the value `FALSE`. The default value of
    `use_spheroid` is `FALSE`.

**Return type**

Polygon `GEOGRAPHY`

**Example**

The following example shows the result of `ST_BUFFER` on a point. A buffered
point is an approximated circle. When `num_seg_quarter_circle = 2`, there are
two line segments in a quarter circle, and therefore the buffered circle has
eight sides and [`ST_NUMPOINTS`][st-numpoints] returns nine vertices. When
`num_seg_quarter_circle = 8`, there are eight line segments in a quarter circle,
and therefore the buffered circle has thirty-two sides and
[`ST_NUMPOINTS`][st-numpoints] returns thirty-three vertices.

```googlesql
SELECT
  -- num_seg_quarter_circle=2
  ST_NUMPOINTS(ST_BUFFER(ST_GEOGFROMTEXT('POINT(1 2)'), 50, 2)) AS eight_sides,
  -- num_seg_quarter_circle=8, since 8 is the default
  ST_NUMPOINTS(ST_BUFFER(ST_GEOGFROMTEXT('POINT(100 2)'), 50)) AS thirty_two_sides;

/*-------------+------------------+
 | eight_sides | thirty_two_sides |
 +-------------+------------------+
 | 9           | 33               |
 +-------------+------------------*/
```

[wgs84-link]: https://en.wikipedia.org/wiki/World_Geodetic_System

[st-bufferwithtolerance]: #st_bufferwithtolerance

[st-numpoints]: #st_numpoints

## `ST_BUFFERWITHTOLERANCE`

```googlesql
ST_BUFFERWITHTOLERANCE(
    geography,
    buffer_radius,
    tolerance_meters => tolerance
    [, use_spheroid => boolean_expression]
    [, endcap => endcap_style]
    [, side => line_side])
```

Returns a `GEOGRAPHY` that represents the buffer around the input `GEOGRAPHY`.
This function is similar to [`ST_BUFFER`][st-buffer],
but you provide tolerance instead of segments to determine how much the
resulting geography can deviate from the ideal buffer radius.

+   `geography`: The input `GEOGRAPHY` to encircle with the buffer radius.
+   `buffer_radius`: `DOUBLE` that represents the radius of the
    buffer around the input geography. The radius is in meters. Note that
    polygons contract when buffered with a negative `buffer_radius`. Polygon
    shells and holes that are contracted to a point are discarded.
+   `tolerance_meters`: `DOUBLE` specifies a tolerance in
    meters with which the shape is approximated. Tolerance determines how much a
    polygon can deviate from the ideal radius. Naming this argument is optional.
+   `endcap`: (Optional) `STRING` allows you to specify one of two endcap
    styles: `ROUND` and `FLAT`. The default value is `ROUND`. This option only
    affects the endcaps of buffered linestrings.
+   `side`: (Optional) `STRING` allows you to specify one of three possible line
    styles: `BOTH`, `LEFT`, and `RIGHT`. The default is `BOTH`. This option only
    affects the endcaps of buffered linestrings.
+   `use_spheroid`: (Optional) `BOOL` determines how this function measures
    distance. If `use_spheroid` is `FALSE`, the function measures distance on
    the surface of a perfect sphere. The `use_spheroid` parameter
    currently only supports the value `FALSE`. The default value of
    `use_spheroid` is `FALSE`.

**Return type**

Polygon `GEOGRAPHY`

**Example**

The following example shows the results of `ST_BUFFERWITHTOLERANCE` on a point,
given two different values for tolerance but with the same buffer radius of
`100`. A buffered point is an approximated circle. When `tolerance_meters=25`,
the tolerance is a large percentage of the buffer radius, and therefore only
five segments are used to approximate a circle around the input point. When
`tolerance_meters=1`, the tolerance is a much smaller percentage of the buffer
radius, and therefore twenty-four edges are used to approximate a circle around
the input point.

```googlesql
SELECT
  -- tolerance_meters=25, or 25% of the buffer radius.
  ST_NumPoints(ST_BUFFERWITHTOLERANCE(ST_GEOGFROMTEXT('POINT(1 2)'), 100, 25)) AS five_sides,
  -- tolerance_meters=1, or 1% of the buffer radius.
  st_NumPoints(ST_BUFFERWITHTOLERANCE(ST_GEOGFROMTEXT('POINT(100 2)'), 100, 1)) AS twenty_four_sides;

/*------------+-------------------+
 | five_sides | twenty_four_sides |
 +------------+-------------------+
 | 6          | 24                |
 +------------+-------------------*/
```

[wgs84-link]: https://en.wikipedia.org/wiki/World_Geodetic_System

[st-buffer]: #st_buffer

## `ST_CENTROID`

```googlesql
ST_CENTROID(geography_expression)
```

**Description**

Returns the _centroid_ of the input `GEOGRAPHY` as a single point `GEOGRAPHY`.

The _centroid_ of a `GEOGRAPHY` is the weighted average of the centroids of the
highest-dimensional components in the `GEOGRAPHY`. The centroid for components
in each dimension is defined as follows:

+   The centroid of points is the arithmetic mean of the input coordinates.
+   The centroid of linestrings is the centroid of all the edges weighted by
    length. The centroid of each edge is the geodesic midpoint of the edge.
+   The centroid of a polygon is its center of mass.

If the input `GEOGRAPHY` is empty, an empty `GEOGRAPHY` is returned.

**Constraints**

In the unlikely event that the centroid of a `GEOGRAPHY` can't be defined by a
single point on the surface of the Earth, a deterministic but otherwise
arbitrary point is returned. This can only happen if the centroid is exactly at
the center of the Earth, such as the centroid for a pair of antipodal points,
and the likelihood of this happening is vanishingly small.

**Return type**

Point `GEOGRAPHY`

## `ST_CLOSESTPOINT`

```googlesql
ST_CLOSESTPOINT(geography_1, geography_2[, use_spheroid])
```

**Description**

Returns a `GEOGRAPHY` containing a point on
`geography_1` with the smallest possible distance to `geography_2`. This implies
that the distance between the point returned by `ST_CLOSESTPOINT` and
`geography_2` is less than or equal to the distance between any other point on
`geography_1` and `geography_2`.

If either of the input `GEOGRAPHY`s is empty, `ST_CLOSESTPOINT` returns `NULL`.

The optional `use_spheroid` parameter determines how this function measures
distance. If `use_spheroid` is `FALSE`, the function measures distance on the
surface of a perfect sphere.

The `use_spheroid` parameter currently only supports
the value `FALSE`. The default value of `use_spheroid` is `FALSE`.

**Return type**

Point `GEOGRAPHY`

[wgs84-link]: https://en.wikipedia.org/wiki/World_Geodetic_System

## `ST_CLUSTERDBSCAN`

```googlesql
ST_CLUSTERDBSCAN(geography_column, epsilon, minimum_geographies)
OVER over_clause

over_clause:
  { named_window | ( [ window_specification ] ) }

window_specification:
  [ named_window ]
  [ PARTITION BY partition_expression [, ...] ]
  [ ORDER BY expression [ { ASC | DESC }  ] [, ...] ]

```

Performs [DBSCAN clustering][dbscan-link] on a column of geographies. Returns a
0-based cluster number.

To learn more about the `OVER` clause and how to use it, see
[Window function calls][window-function-calls].

<!-- mdlint off(WHITESPACE_LINE_LENGTH) -->

[window-function-calls]: https://github.com/google/googlesql/blob/master/docs/window-function-calls.md

<!-- mdlint on -->

**Input parameters**

+   `geography_column`: A column of `GEOGRAPHY`s that
    is clustered.
+   `epsilon`: The epsilon that specifies the radius, measured in meters, around
    a core value. Non-negative `DOUBLE` value.
+   `minimum_geographies`: Specifies the minimum number of geographies in a
    single cluster. Only dense input forms a cluster, otherwise it's classified
    as noise. Non-negative `INT64` value.

**Geography types and the DBSCAN algorithm**

The DBSCAN algorithm identifies high-density clusters of data and marks outliers
in low-density areas of noise. Geographies passed in through `geography_column`
are classified in one of three ways by the DBSCAN algorithm:

+   Core value: A geography is a core value if it's within `epsilon` distance
    of `minimum_geographies` geographies, including itself. The core value
    starts a new cluster, or is added to the same cluster as a core value within
    `epsilon` distance. Core values are grouped in a cluster together with all
    other core and border values that are within `epsilon` distance.
+   Border value: A geography is a border value if it's within epsilon distance
    of a core value. It's added to the same cluster as a core value within
    `epsilon` distance. A border value may be within `epsilon` distance of more
    than one cluster. In this case, it may be arbitrarily assigned to either
    cluster and the function will produce the same result in subsequent calls.
+   Noise: A geography is noise if it's neither a core nor a border value.
    Noise values are assigned to a `NULL` cluster. An empty
    `GEOGRAPHY` is always classified as noise.

**Constraints**

+   The argument `minimum_geographies` is a non-negative
    `INT64`and `epsilon` is a non-negative
    `DOUBLE`.
+   An empty geography can't join any cluster.
+   Multiple clustering assignments could be possible for a border value. If a
    geography is a border value, `ST_CLUSTERDBSCAN` will assign it to an
    arbitrary valid cluster.

**Return type**

`INT64` for each geography in the geography column.

**Examples**

This example performs DBSCAN clustering with a radius of 100,000 meters with a
`minimum_geographies` argument of 1. The geographies being analyzed are a
mixture of points, lines, and polygons.

```googlesql
WITH Geos as
  (SELECT 1 as row_id, ST_GEOGFROMTEXT('POINT EMPTY') as geo UNION ALL
    SELECT 2, ST_GEOGFROMTEXT('MULTIPOINT(1 1, 2 2, 4 4, 5 2)') UNION ALL
    SELECT 3, ST_GEOGFROMTEXT('POINT(14 15)') UNION ALL
    SELECT 4, ST_GEOGFROMTEXT('LINESTRING(40 1, 42 34, 44 39)') UNION ALL
    SELECT 5, ST_GEOGFROMTEXT('POLYGON((40 2, 40 1, 41 2, 40 2))'))
SELECT row_id, geo, ST_CLUSTERDBSCAN(geo, 1e5, 1) OVER () AS cluster_num FROM
Geos ORDER BY row_id

/*--------+-----------------------------------+-------------+
 | row_id |                geo                | cluster_num |
 +--------+-----------------------------------+-------------+
 | 1      | GEOMETRYCOLLECTION EMPTY          | NULL        |
 | 2      | MULTIPOINT(1 1, 2 2, 5 2, 4 4)    | 0           |
 | 3      | POINT(14 15)                      | 1           |
 | 4      | LINESTRING(40 1, 42 34, 44 39)    | 2           |
 | 5      | POLYGON((40 2, 40 1, 41 2, 40 2)) | 2           |
 +--------+-----------------------------------+-------------*/
```

[dbscan-link]: https://en.wikipedia.org/wiki/DBSCAN

## `ST_CONTAINS`

```googlesql
ST_CONTAINS(geography_1, geography_2)
```

**Description**

Returns `TRUE` if no point of `geography_2` is outside `geography_1`, and
the interiors intersect; returns `FALSE` otherwise.

NOTE: A `GEOGRAPHY` *does not* contain its own
boundary. Compare with [`ST_COVERS`][st_covers].

**Return type**

`BOOL`

**Example**

The following query tests whether the polygon `POLYGON((1 1, 20 1, 10 20, 1 1))`
contains each of the three points `(0, 0)`, `(1, 1)`, and `(10, 10)`, which lie
on the exterior, the boundary, and the interior of the polygon respectively.

```googlesql
SELECT
  ST_GEOGPOINT(i, i) AS p,
  ST_CONTAINS(ST_GEOGFROMTEXT('POLYGON((1 1, 20 1, 10 20, 1 1))'),
              ST_GEOGPOINT(i, i)) AS `contains`
FROM UNNEST([0, 1, 10]) AS i;

/*--------------+----------+
 | p            | contains |
 +--------------+----------+
 | POINT(0 0)   | FALSE    |
 | POINT(1 1)   | FALSE    |
 | POINT(10 10) | TRUE     |
 +--------------+----------*/
```

[st_covers]: #st_covers

## `ST_CONVEXHULL`

```googlesql
ST_CONVEXHULL(geography_expression)
```

**Description**

Returns the convex hull for the input `GEOGRAPHY`. The convex hull is the
smallest convex `GEOGRAPHY` that covers the input. A `GEOGRAPHY` is convex if
for every pair of points in the `GEOGRAPHY`, the geodesic edge connecting the
points are also contained in the same `GEOGRAPHY`.

In most cases, the convex hull consists of a single polygon. Notable edge cases
include the following:

*   The convex hull of a single point is also a point.
*   The convex hull of two or more collinear points is a linestring as long as
    that linestring is convex.
*   If the input `GEOGRAPHY` spans more than a
    hemisphere, the convex hull is the full globe. This includes any input that
    contains a pair of antipodal points.
*   `ST_CONVEXHULL` returns `NULL` if the input is either `NULL` or the empty
    `GEOGRAPHY`.

**Return type**

`GEOGRAPHY`

**Examples**

The convex hull returned by `ST_CONVEXHULL` can be a point, linestring, or a
polygon, depending on the input.

```googlesql
WITH Geographies AS
 (SELECT ST_GEOGFROMTEXT('POINT(1 1)') AS g UNION ALL
  SELECT ST_GEOGFROMTEXT('LINESTRING(1 1, 2 2)') AS g UNION ALL
  SELECT ST_GEOGFROMTEXT('MULTIPOINT(2 11, 4 12, 0 15, 1 9, 1 12)') AS g)
SELECT
  g AS input_geography,
  ST_CONVEXHULL(g) AS convex_hull
FROM Geographies;

/*-----------------------------------------+--------------------------------------------------------+
 |             input_geography             |                      convex_hull                       |
 +-----------------------------------------+--------------------------------------------------------+
 | POINT(1 1)                              | POINT(0.999999999999943 1)                             |
 | LINESTRING(1 1, 2 2)                    | LINESTRING(2 2, 1.49988573656168 1.5000570914792, 1 1) |
 | MULTIPOINT(1 9, 4 12, 2 11, 1 12, 0 15) | POLYGON((1 9, 4 12, 0 15, 1 9))                        |
 +-----------------------------------------+--------------------------------------------------------*/
```

## `ST_COVEREDBY`

```googlesql
ST_COVEREDBY(geography_1, geography_2)
```

**Description**

Returns `FALSE` if `geography_1` or `geography_2` is empty. Returns `TRUE` if no
points of `geography_1` lie in the exterior of `geography_2`.

Given two `GEOGRAPHY`s `a` and `b`,
`ST_COVEREDBY(a, b)` returns the same result as
[`ST_COVERS`][st-covers]`(b, a)`. Note the opposite order of arguments.

**Return type**

`BOOL`

[st-covers]: #st_covers

## `ST_COVERS`

```googlesql
ST_COVERS(geography_1, geography_2)
```

**Description**

Returns `FALSE` if `geography_1` or `geography_2` is empty.
Returns `TRUE` if no points of `geography_2` lie in the exterior of
`geography_1`.

**Return type**

`BOOL`

**Example**

The following query tests whether the polygon `POLYGON((1 1, 20 1, 10 20, 1 1))`
covers each of the three points `(0, 0)`, `(1, 1)`, and `(10, 10)`, which lie
on the exterior, the boundary, and the interior of the polygon respectively.

```googlesql
SELECT
  ST_GEOGPOINT(i, i) AS p,
  ST_COVERS(ST_GEOGFROMTEXT('POLYGON((1 1, 20 1, 10 20, 1 1))'),
            ST_GEOGPOINT(i, i)) AS `covers`
FROM UNNEST([0, 1, 10]) AS i;

/*--------------+--------+
 | p            | covers |
 +--------------+--------+
 | POINT(0 0)   | FALSE  |
 | POINT(1 1)   | TRUE   |
 | POINT(10 10) | TRUE   |
 +--------------+--------*/
```

## `ST_DIFFERENCE`

```googlesql
ST_DIFFERENCE(geography_1, geography_2)
```

**Description**

Returns a `GEOGRAPHY` that represents the point set
difference of `geography_1` and `geography_2`. Therefore, the result consists of
the part of `geography_1` that doesn't intersect with `geography_2`.

If `geometry_1` is completely contained in `geometry_2`, then `ST_DIFFERENCE`
returns an empty `GEOGRAPHY`.

**Constraints**

The underlying geometric objects that a GoogleSQL
`GEOGRAPHY` represents correspond to a *closed* point
set. Therefore, `ST_DIFFERENCE` is the closure of the point set difference of
`geography_1` and `geography_2`. This implies that if `geography_1` and
`geography_2` intersect, then a portion of the boundary of `geography_2` could
be in the difference.

**Return type**

`GEOGRAPHY`

**Example**

The following query illustrates the difference between `geog1`, a larger polygon
`POLYGON((0 0, 10 0, 10 10, 0 0))` and `geog2`, a smaller polygon
`POLYGON((4 2, 6 2, 8 6, 4 2))` that intersects with `geog1`. The result is
`geog1` with a hole where `geog2` intersects with it.

```googlesql
SELECT
  ST_DIFFERENCE(
      ST_GEOGFROMTEXT('POLYGON((0 0, 10 0, 10 10, 0 0))'),
      ST_GEOGFROMTEXT('POLYGON((4 2, 6 2, 8 6, 4 2))')
  );

/*--------------------------------------------------------+
 | difference_of_geog1_and_geog2                          |
 +--------------------------------------------------------+
 | POLYGON((0 0, 10 0, 10 10, 0 0), (8 6, 6 2, 4 2, 8 6)) |
 +--------------------------------------------------------*/
```

## `ST_DIMENSION`

```googlesql
ST_DIMENSION(geography_expression)
```

**Description**

Returns the dimension of the highest-dimensional element in the input
`GEOGRAPHY`.

The dimension of each possible element is as follows:

+   The dimension of a point is `0`.
+   The dimension of a linestring is `1`.
+   The dimension of a polygon is `2`.

If the input `GEOGRAPHY` is empty, `ST_DIMENSION`
returns `-1`.

**Return type**

`INT64`

## `ST_DISJOINT`

```googlesql
ST_DISJOINT(geography_1, geography_2)
```

**Description**

Returns `TRUE` if the intersection of `geography_1` and `geography_2` is empty,
that is, no point in `geography_1` also appears in `geography_2`.

`ST_DISJOINT` is the logical negation of [`ST_INTERSECTS`][st-intersects].

**Return type**

`BOOL`

[st-intersects]: #st_intersects

## `ST_DISTANCE`

```
ST_DISTANCE(geography_1, geography_2[, use_spheroid])
```

**Description**

Returns the shortest distance in meters between two non-empty
`GEOGRAPHY`s.

If either of the input `GEOGRAPHY`s is empty,
`ST_DISTANCE` returns `NULL`.

The optional `use_spheroid` parameter determines how this function measures
distance. If `use_spheroid` is `FALSE`, the function measures distance on the
surface of a perfect sphere. If `use_spheroid` is `TRUE`, the function measures
distance on the surface of the [WGS84][wgs84-link] spheroid. The default value
of `use_spheroid` is `FALSE`.

**Return type**

`DOUBLE`

[wgs84-link]: https://en.wikipedia.org/wiki/World_Geodetic_System

## `ST_DUMP`

```googlesql
ST_DUMP(geography[, dimension])
```

**Description**

Returns an `ARRAY` of simple
`GEOGRAPHY`s where each element is a component of
the input `GEOGRAPHY`. A simple
`GEOGRAPHY` consists of a single point, linestring,
or polygon. If the input `GEOGRAPHY` is simple, the
result is a single element. When the input
`GEOGRAPHY` is a collection, `ST_DUMP` returns an
`ARRAY` with one simple
`GEOGRAPHY` for each component in the collection.

If `dimension` is provided, the function only returns
`GEOGRAPHY`s of the corresponding dimension. A
dimension of -1 is equivalent to omitting `dimension`.

**Return Type**

`ARRAY<GEOGRAPHY>`

**Examples**

The following example shows how `ST_DUMP` returns the simple geographies within
a complex geography.

```googlesql
WITH example AS (
  SELECT ST_GEOGFROMTEXT('POINT(0 0)') AS geography
  UNION ALL
  SELECT ST_GEOGFROMTEXT('MULTIPOINT(0 0, 1 1)') AS geography
  UNION ALL
  SELECT ST_GEOGFROMTEXT('GEOMETRYCOLLECTION(POINT(0 0), LINESTRING(1 2, 2 1))'))
SELECT
  geography AS original_geography,
  ST_DUMP(geography) AS dumped_geographies
FROM example

/*-------------------------------------+------------------------------------+
 |         original_geographies        |      dumped_geographies            |
 +-------------------------------------+------------------------------------+
 | POINT(0 0)                          | [POINT(0 0)]                       |
 | MULTIPOINT(0 0, 1 1)                | [POINT(0 0), POINT(1 1)]           |
 | GEOMETRYCOLLECTION(POINT(0 0),      | [POINT(0 0), LINESTRING(1 2, 2 1)] |
 |   LINESTRING(1 2, 2 1))             |                                    |
 +-------------------------------------+------------------------------------*/
```

The following example shows how `ST_DUMP` with the dimension argument only
returns simple geographies of the given dimension.

```googlesql
WITH example AS (
  SELECT ST_GEOGFROMTEXT('GEOMETRYCOLLECTION(POINT(0 0), LINESTRING(1 2, 2 1))') AS geography)
SELECT
  geography AS original_geography,
  ST_DUMP(geography, 1) AS dumped_geographies
FROM example

/*-------------------------------------+------------------------------+
 |         original_geographies        |      dumped_geographies      |
 +-------------------------------------+------------------------------+
 | GEOMETRYCOLLECTION(POINT(0 0),      | [LINESTRING(1 2, 2 1)]       |
 |   LINESTRING(1 2, 2 1))             |                              |
 +-------------------------------------+------------------------------*/
```

## `ST_DUMPPOINTS`

```googlesql
ST_DUMPPOINTS(geography)
```

**Description**

Takes an input geography and returns all of its points, line vertices, and
polygon vertices as an array of point geographies.

**Return Type**

`ARRAY<Point GEOGRAPHY>`

**Examples**

```googlesql
WITH example AS (
  SELECT ST_GEOGFROMTEXT('POINT(0 0)') AS geography
  UNION ALL
  SELECT ST_GEOGFROMTEXT('MULTIPOINT(0 0, 1 1)') AS geography
  UNION ALL
  SELECT ST_GEOGFROMTEXT('GEOMETRYCOLLECTION(POINT(0 0), LINESTRING(1 2, 2 1))'))
SELECT
  geography AS original_geography,
  ST_DUMPPOINTS(geography) AS dumped_points_geographies
FROM example

/*-------------------------------------+------------------------------------+
 | original_geographies                | dumped_points_geographies          |
 +-------------------------------------+------------------------------------+
 | POINT(0 0)                          | [POINT(0 0)]                       |
 | MULTIPOINT(0 0, 1 1)                | [POINT(0 0),POINT(1 1)]            |
 | GEOMETRYCOLLECTION(POINT(0 0),      | [POINT(0 0),POINT(1 2),POINT(2 1)] |
 |   LINESTRING(1 2, 2 1))             |                                    |
 +-------------------------------------+------------------------------------*/
```

## `ST_DWITHIN`

```googlesql
ST_DWITHIN(geography_1, geography_2, distance[, use_spheroid])
```

**Description**

Returns `TRUE` if the distance between at least one point in `geography_1` and
one point in `geography_2` is less than or equal to the distance given by the
`distance` argument; otherwise, returns `FALSE`. If either input
`GEOGRAPHY` is empty, `ST_DWithin` returns `FALSE`. The
given `distance` is in meters on the surface of the Earth.

The optional `use_spheroid` parameter determines how this function measures
distance. If `use_spheroid` is `FALSE`, the function measures distance on the
surface of a perfect sphere.

The `use_spheroid` parameter currently only supports
the value `FALSE`. The default value of `use_spheroid` is `FALSE`.

**Return type**

`BOOL`

[wgs84-link]: https://en.wikipedia.org/wiki/World_Geodetic_System

## `ST_ENDPOINT`

```googlesql
ST_ENDPOINT(linestring_geography)
```

**Description**

Returns the last point of a linestring geography as a point geography. Returns
an error if the input isn't a linestring or if the input is empty. Use the
`SAFE` prefix to obtain `NULL` for invalid input instead of an error.

**Return Type**

Point `GEOGRAPHY`

**Example**

```googlesql
SELECT ST_ENDPOINT(ST_GEOGFROMTEXT('LINESTRING(1 1, 2 1, 3 2, 3 3)')) last

/*--------------+
 | last         |
 +--------------+
 | POINT(3 3)   |
 +--------------*/
```

## `ST_EQUALS`

```googlesql
ST_EQUALS(geography_1, geography_2)
```

**Description**

Checks if two `GEOGRAPHY` values represent the same `GEOGRAPHY` value. Returns
`TRUE` if the values are the same, otherwise returns `FALSE`.

**Definitions**

+ `geography_1`: The first `GEOGRAPHY` value to compare.
+ `geography_2`: The second `GEOGRAPHY` value to compare.

**Details**

As long as they still represent the same geometric structure, two
`GEOGRAPHY` values can be equal even if the ordering of points or vertices
differ. This means that one of the following conditions must be true for this
function to return `TRUE`:

+   Both `ST_COVERS(geography_1, geography_2)` and
    `ST_COVERS(geography_2, geography_1)` are `TRUE`.
+   Both `geography_1` and `geography_2` are empty.

`ST_EQUALS` isn't guaranteed to be a transitive function.

**Return type**

`BOOL`

## `ST_EXTENT`

```googlesql
ST_EXTENT(geography_expression)
```

**Description**

Returns a `STRUCT` that represents the bounding box for the set of input
`GEOGRAPHY` values. The bounding box is the minimal rectangle that encloses the
geography. The edges of the rectangle follow constant lines of longitude and
latitude.

Caveats:

+ Returns `NULL` if all the inputs are `NULL` or empty geographies.
+ The bounding box might cross the antimeridian if this allows for a smaller
  rectangle. In this case, the bounding box has one of its longitudinal bounds
  outside of the [-180, 180] range, so that `xmin` is smaller than the eastmost
  value `xmax`.
+ If the longitude span of the bounding box is larger than or equal to 180
  degrees, the function returns the bounding box with the longitude range of
  [-180, 180].

**Return type**

`STRUCT<xmin DOUBLE, ymin DOUBLE, xmax DOUBLE, ymax DOUBLE>`.

Bounding box parts:

+ `xmin`: The westmost constant longitude line that bounds the rectangle.
+ `xmax`: The eastmost constant longitude line that bounds the rectangle.
+ `ymin`: The minimum constant latitude line that bounds the rectangle.
+ `ymax`: The maximum constant latitude line that bounds the rectangle.

**Example**

```googlesql
WITH data AS (
  SELECT 1 id, ST_GEOGFROMTEXT('POLYGON((-125 48, -124 46, -117 46, -117 49, -125 48))') g
  UNION ALL
  SELECT 2 id, ST_GEOGFROMTEXT('POLYGON((172 53, -130 55, -141 70, 172 53))') g
  UNION ALL
  SELECT 3 id, ST_GEOGFROMTEXT('POINT EMPTY') g
)
SELECT ST_EXTENT(g) AS box
FROM data

/*----------------------------------------------+
 | box                                          |
 +----------------------------------------------+
 | {xmin:172, ymin:46, xmax:243, ymax:70}       |
 +----------------------------------------------*/
```

[`ST_BOUNDINGBOX`][st-boundingbox] for the non-aggregate version of `ST_EXTENT`.

[st-boundingbox]: #st_boundingbox

## `ST_EXTERIORRING`

```googlesql
ST_EXTERIORRING(polygon_geography)
```

**Description**

Returns a linestring geography that corresponds to the outermost ring of a
polygon geography.

+   If the input geography is a polygon, gets the outermost ring of the polygon
    geography and returns the corresponding linestring.
+   If the input is the full `GEOGRAPHY`, returns an empty geography.
+   Returns an error if the input isn't a single polygon.

Use the `SAFE` prefix to return `NULL` for invalid input instead of an error.

**Return type**

+ Linestring `GEOGRAPHY`
+ Empty `GEOGRAPHY`

**Examples**

```googlesql
WITH geo as
 (SELECT ST_GEOGFROMTEXT('POLYGON((0 0, 1 4, 2 2, 0 0))') AS g UNION ALL
  SELECT ST_GEOGFROMTEXT('''POLYGON((1 1, 1 10, 5 10, 5 1, 1 1),
                                  (2 2, 3 4, 2 4, 2 2))''') as g)
SELECT ST_EXTERIORRING(g) AS ring FROM geo;

/*---------------------------------------+
 | ring                                  |
 +---------------------------------------+
 | LINESTRING(2 2, 1 4, 0 0, 2 2)        |
 | LINESTRING(5 1, 5 10, 1 10, 1 1, 5 1) |
 +---------------------------------------*/
```

## `ST_GEOGFROM`

```googlesql
ST_GEOGFROM(expression)
```

**Description**

Converts an expression for a `STRING` or `BYTES` value into a
`GEOGRAPHY` value.

If `expression` represents a `STRING` value, it must be a valid
`GEOGRAPHY` representation in one of the following formats:

+ WKT format. To learn more about this format and the requirements to use it,
  see [ST_GEOGFROMTEXT][st-geogfromtext].
+ WKB in hexadecimal text format. To learn more about this format and the
  requirements to use it, see [ST_GEOGFROMWKB][st-geogfromwkb].
+ GeoJSON format. To learn more about this format and the
  requirements to use it, see [ST_GEOGFROMGEOJSON][st-geogfromgeojson].

If `expression` represents a `BYTES` value, it must be a valid `GEOGRAPHY`
binary expression in WKB format. To learn more about this format and the
requirements to use it, see [ST_GEOGFROMWKB][st-geogfromwkb].

If `expression` is `NULL`, the output is `NULL`.

**Return type**

`GEOGRAPHY`

**Examples**

This takes a WKT-formatted string and returns a `GEOGRAPHY` polygon:

```googlesql
SELECT ST_GEOGFROM('POLYGON((0 0, 0 2, 2 2, 2 0, 0 0))') AS WKT_format;

/*------------------------------------+
 | WKT_format                         |
 +------------------------------------+
 | POLYGON((2 0, 2 2, 0 2, 0 0, 2 0)) |
 +------------------------------------*/
```

This takes a WKB-formatted hexadecimal-encoded string and returns a
`GEOGRAPHY` point:

```googlesql
SELECT ST_GEOGFROM(FROM_HEX('010100000000000000000000400000000000001040')) AS WKB_format;

/*----------------+
 | WKB_format     |
 +----------------+
 | POINT(2 4)     |
 +----------------*/
```

This takes WKB-formatted bytes and returns a `GEOGRAPHY` point:

```googlesql
SELECT ST_GEOGFROM('010100000000000000000000400000000000001040') AS WKB_format;

/*----------------+
 | WKB_format     |
 +----------------+
 | POINT(2 4)     |
 +----------------*/
```

This takes a GeoJSON-formatted string and returns a `GEOGRAPHY` polygon:

```googlesql
SELECT ST_GEOGFROM(
  '{ "type": "Polygon", "coordinates": [ [ [2, 0], [2, 2], [1, 2], [0, 2], [0, 0], [2, 0] ] ] }'
) AS GEOJSON_format;

/*-----------------------------------------+
 | GEOJSON_format                          |
 +-----------------------------------------+
 | POLYGON((2 0, 2 2, 1 2, 0 2, 0 0, 2 0)) |
 +-----------------------------------------*/
```

[st-geogfromtext]: #st_geogfromtext

[st-geogfromwkb]: #st_geogfromwkb

[st-geogfromgeojson]: #st_geogfromgeojson

## `ST_GEOGFROMGEOJSON`

```googlesql
ST_GEOGFROMGEOJSON(
  geojson_string
  [, make_valid => constant_expression ]
)
```

**Description**

Returns a `GEOGRAPHY` value that corresponds to the
input [GeoJSON][geojson-link] representation.

`ST_GEOGFROMGEOJSON` accepts input that's [RFC 7946][geojson-spec-link]
compliant.

If the named argument `make_valid` is set to `TRUE`, the function attempts to
repair polygons that don't conform to [Open Geospatial Consortium][ogc-link]
semantics.

A GoogleSQL `GEOGRAPHY` has spherical
geodesic edges, whereas a GeoJSON `Geometry` object explicitly has planar edges.
To convert between these two types of edges, GoogleSQL adds additional
points to the line where necessary so that the resulting sequence of edges
remains within 10 meters of the original edge.

See [`ST_ASGEOJSON`][st-asgeojson] to format a
`GEOGRAPHY` as GeoJSON.

**Constraints**

The JSON input is subject to the following constraints:

+   `ST_GEOGFROMGEOJSON` only accepts JSON geometry fragments and can't be used
    to ingest a whole JSON document.
+   The input JSON fragment must consist of a GeoJSON geometry type, which
    includes `Point`, `MultiPoint`, `LineString`, `MultiLineString`, `Polygon`,
    `MultiPolygon`, and `GeometryCollection`. Any other GeoJSON type such as
    `Feature` or `FeatureCollection` will result in an error.
+   A position in the `coordinates` member of a GeoJSON geometry type must
    consist of exactly two elements. The first is the longitude and the second
    is the latitude. Therefore, `ST_GEOGFROMGEOJSON` doesn't support the
    optional third element for a position in the `coordinates` member.

**Return type**

`GEOGRAPHY`

[geojson-link]: https://en.wikipedia.org/wiki/GeoJSON

[geojson-spec-link]: https://tools.ietf.org/html/rfc7946

[ogc-link]: https://www.ogc.org/standards/sfa

[st-asgeojson]: #st_asgeojson

## `ST_GEOGFROMKML`

```googlesql
ST_GEOGFROMKML(kml_geometry)
```

Takes a `STRING` [KML geometry][kml-geometry-link] and returns a
`GEOGRAPHY`. The KML geomentry can include:

+  Point with coordinates element only
+  Linestring with coordinates element only
+  Polygon with boundary elements only
+  Multigeometry

[kml-geometry-link]: https://developers.google.com/kml/documentation/kmlreference#geometry

## `ST_GEOGFROMTEXT`

<a id="st_geogfromtext_signature1"></a><a id="st_geogfromtext_signature2"></a>

```googlesql
ST_GEOGFROMTEXT(
  wkt_string
  [ , oriented => value ]
  [ , planar => value ]
  [ , make_valid => value ]
)
```

**Description**

Converts a `STRING` [WKT][wkt-link] geometry value into a `GEOGRAPHY`
value.

To format `GEOGRAPHY` value as WKT, use [`ST_ASTEXT`][st-astext].

**Definitions**

+   `wkt_string`: A `STRING` value that contains the [WKT][wkt-link] format.
+   `oriented`: A named argument with a `BOOL` literal.

    +   If the value is `TRUE`, any polygons in the input are assumed to be
        oriented as follows: when traveling along the boundary of the polygon
        in the order of the input vertices, the interior of the polygon is on
        the left. This allows WKT to represent polygons larger than a
        hemisphere. See also [`ST_MAKEPOLYGONORIENTED`][st-makepolygonoriented],
        which is similar to `ST_GEOGFROMTEXT` with `oriented=TRUE`.

    +   If the value is `FALSE` or omitted, this function returns the polygon
        with the smaller area.
+   `planar`: A named argument with a `BOOL` literal. If the value
    is `TRUE`, the edges of the linestrings and polygons are assumed to use
    planar map semantics, rather than GoogleSQL default spherical
    geodesics semantics.
+   `make_valid`: A named argument with a `BOOL` literal. If the
    value is `TRUE`, the function attempts to repair polygons that don't
    conform to [Open Geospatial Consortium][ogc-link] semantics.

**Details**

+   The function doesn't support three-dimensional geometries that have a `Z`
    suffix, nor does it support linear referencing system geometries with an `M`
    suffix.
+   `oriented` and `planar` can't be `TRUE` at the same time.
+   `oriented` and `make_valid` can't be `TRUE` at the same time.

**Example**

The following query reads the WKT string `POLYGON((0 0, 0 2, 2 2, 0 2, 0 0))`
both as a non-oriented polygon and as an oriented polygon, and checks whether
each result contains the point `(1, 1)`.

```googlesql
WITH polygon AS (SELECT 'POLYGON((0 0, 0 2, 2 2, 2 0, 0 0))' AS p)
SELECT
  ST_CONTAINS(ST_GEOGFROMTEXT(p), ST_GEOGPOINT(1, 1)) AS fromtext_default,
  ST_CONTAINS(ST_GEOGFROMTEXT(p, oriented => FALSE), ST_GEOGPOINT(1, 1)) AS non_oriented,
  ST_CONTAINS(ST_GEOGFROMTEXT(p, oriented => TRUE),  ST_GEOGPOINT(1, 1)) AS oriented
FROM polygon;

/*-------------------+---------------+-----------+
 | fromtext_default  | non_oriented  | oriented  |
 +-------------------+---------------+-----------+
 | TRUE              | TRUE          | FALSE     |
 +-------------------+---------------+-----------*/
```

The following query converts a WKT string with an invalid polygon to
`GEOGRAPHY`. The WKT string violates two properties
of a valid polygon - the loop describing the polygon isn't closed, and it
contains self-intersection. With the `make_valid` option, `ST_GEOGFROMTEXT`
successfully converts it to a multipolygon shape.

```googlesql
WITH data AS (
  SELECT 'POLYGON((0 -1, 2 1, 2 -1, 0 1))' wkt)
SELECT
  SAFE.ST_GEOGFROMTEXT(wkt) as geom,
  SAFE.ST_GEOGFROMTEXT(wkt, make_valid => TRUE) as valid_geom
FROM data

/*------+-----------------------------------------------------------------+
 | geom | valid_geom                                                      |
 +------+-----------------------------------------------------------------+
 | NULL | MULTIPOLYGON(((0 -1, 1 0, 0 1, 0 -1)), ((1 0, 2 -1, 2 1, 1 0))) |
 +------+-----------------------------------------------------------------*/
```

[ogc-link]: https://www.ogc.org/standards/sfa

[wkt-link]: https://en.wikipedia.org/wiki/Well-known_text

[st-makepolygonoriented]: #st_makepolygonoriented

[st-astext]: #st_astext

[st-geogfromgeojson]: #st_geogfromgeojson

## `ST_GEOGFROMWKB`

```googlesql
ST_GEOGFROMWKB(
  wkb_bytes_expression
  [ , oriented => value ]
  [ , planar => value ]
  [ , make_valid => value ]
)
```

```googlesql
ST_GEOGFROMWKB(
  wkb_hex_string_expression
  [, oriented => value ]
  [, planar => value ]
  [, make_valid => value ]
)
```

**Description**

Converts an expression from a hexadecimal-text `STRING` or `BYTES`
value into a `GEOGRAPHY` value. The expression must be in
[WKB][wkb-link] format.

To format `GEOGRAPHY` as WKB, use [`ST_ASBINARY`][st-asbinary].

**Definitions**

+   `wkb_bytes_expression`: A `BYTES` value that contains the [WKB][wkb-link]
    format.
+   `wkb_hex_string_expression`: A `STRING` value that contains the
    hexadecimal-encoded [WKB][wkb-link] format.
+   `oriented`: A named argument with a `BOOL` literal.

    +   If the value is `TRUE`, any polygons in the input are assumed to be
        oriented as follows: when traveling along the boundary of the polygon
        in the order of the input vertices, the interior of the polygon is on
        the left. This allows WKB to represent polygons larger than a
        hemisphere. See also [`ST_MAKEPOLYGONORIENTED`][st-makepolygonoriented],
        which is similar to `ST_GEOGFROMWKB` with `oriented=TRUE`.

    +   If the value is `FALSE` or omitted, this function returns the polygon
        with the smaller area.
+   `planar`: A named argument with a `BOOL` literal. If the value
    is `TRUE`, the edges of the linestrings and polygons are assumed to use
    planar map semantics, rather than GoogleSQL default spherical
    geodesics semantics.
+   `make_valid`: A named argument with a `BOOL` literal. If the
    value is `TRUE`, the function attempts to repair polygons that
    don't conform to [Open Geospatial Consortium][ogc-link] semantics.

**Details**

+   The function doesn't support three-dimensional geometries that have a `Z`
    suffix, nor does it support linear referencing system geometries with an `M`
    suffix.
+   `oriented` and `planar` can't be `TRUE` at the same time.
+   `oriented` and `make_valid` can't be `TRUE` at the same time.

**Return type**

`GEOGRAPHY`

**Example**

The following query reads the hex-encoded WKB data containing
`LINESTRING(1 1, 3 2)` and uses it with planar and geodesic semantics. When
planar is used, the function approximates the planar input line using
line that contains a chain of geodesic segments.

```googlesql
WITH wkb_data AS (
  SELECT '010200000002000000feffffffffffef3f000000000000f03f01000000000008400000000000000040' geo
)
SELECT
  ST_GeogFromWkb(geo, planar=>TRUE) AS from_planar,
  ST_GeogFromWkb(geo, planar=>FALSE) AS from_geodesic,
FROM wkb_data

/*---------------------------------------+----------------------+
 | from_planar                           | from_geodesic        |
 +---------------------------------------+----------------------+
 | LINESTRING(1 1, 2 1.5, 2.5 1.75, 3 2) | LINESTRING(1 1, 3 2) |
 +---------------------------------------+----------------------*/
```

[wkb-link]: https://en.wikipedia.org/wiki/Well-known_text#Well-known_binary

[st-asbinary]: #st_asbinary

[st-geogfromgeojson]: #st_geogfromgeojson

## `ST_GEOGPOINT`

```googlesql
ST_GEOGPOINT(longitude, latitude)
```

**Description**

Creates a `GEOGRAPHY` with a single point. `ST_GEOGPOINT` creates a point from
the specified `DOUBLE` longitude (in degrees,
negative west of the Prime Meridian, positive east) and latitude (in degrees,
positive north of the Equator, negative south) parameters and returns that point
in a `GEOGRAPHY` value.

NOTE: Some systems present latitude first; take care with argument order.

**Constraints**

+   Longitudes outside the range \[-180, 180\] are allowed; `ST_GEOGPOINT` uses
    the input longitude modulo 360 to obtain a longitude within \[-180, 180\].
+   Latitudes must be in the range \[-90, 90\]. Latitudes outside this range
    will result in an error.

**Return type**

Point `GEOGRAPHY`

## `ST_GEOGPOINTFROMGEOHASH`

```googlesql
ST_GEOGPOINTFROMGEOHASH(geohash)
```

**Description**

Returns a `GEOGRAPHY` value that corresponds to a
point in the middle of a bounding box defined in the [GeoHash][geohash-link].

**Return type**

Point `GEOGRAPHY`

[geohash-link]: https://en.wikipedia.org/wiki/Geohash

## `ST_GEOHASH`

```googlesql
ST_GEOHASH(geography_expression[, maxchars])
```

**Description**

Takes a single-point `GEOGRAPHY` and returns a [GeoHash][geohash-link]
representation of that `GEOGRAPHY` object.

+   `geography_expression`: Represents a `GEOGRAPHY` object. Only a `GEOGRAPHY`
    object that represents a single point is supported. If `ST_GEOHASH` is used
    over an empty `GEOGRAPHY` object, returns `NULL`.
+   `maxchars`: This optional `INT64` parameter specifies the maximum number of
    characters the hash will contain. Fewer characters corresponds to lower
    precision (or, described differently, to a bigger bounding box). `maxchars`
    defaults to 20 if not explicitly specified. A valid `maxchars` value is 1
    to 20. Any value below or above is considered unspecified and the default of
    20 is used.

**Return type**

`STRING`

**Example**

Returns a GeoHash of the Seattle Center with 10 characters of precision.

```googlesql
SELECT ST_GEOHASH(ST_GEOGPOINT(-122.35, 47.62), 10) geohash

/*--------------+
 | geohash      |
 +--------------+
 | c22yzugqw7   |
 +--------------*/
```

[geohash-link]: https://en.wikipedia.org/wiki/Geohash

## `ST_GEOMETRYTYPE`

```googlesql
ST_GEOMETRYTYPE(geography_expression)
```

**Description**

Returns the [Open Geospatial Consortium][ogc-link] (OGC) geometry type that
describes the input `GEOGRAPHY`. The OGC geometry type matches the
types that are used in [WKT][wkt-link] and [GeoJSON][geojson-link] formats and
printed for [ST_ASTEXT][st-astext] and [ST_ASGEOJSON][st-asgeojson].
`ST_GEOMETRYTYPE` returns the OGC geometry type with the "ST_" prefix.

`ST_GEOMETRYTYPE` returns the following given the type on the input:

+   Single point geography: Returns `ST_Point`.
+   Collection of only points: Returns `ST_MultiPoint`.
+   Single linestring geography: Returns `ST_LineString`.
+   Collection of only linestrings: Returns `ST_MultiLineString`.
+   Single polygon geography: Returns `ST_Polygon`.
+   Collection of only polygons: Returns `ST_MultiPolygon`.
+   Collection with elements of different dimensions, or the input is the empty
    geography: Returns `ST_GeometryCollection`.

**Return type**

`STRING`

**Example**

The following example shows how `ST_GEOMETRYTYPE` takes geographies and returns
the names of their OGC geometry types.

```googlesql
WITH example AS(
  SELECT ST_GEOGFROMTEXT('POINT(0 1)') AS geography
  UNION ALL
  SELECT ST_GEOGFROMTEXT('MULTILINESTRING((2 2, 3 4), (5 6, 7 7))')
  UNION ALL
  SELECT ST_GEOGFROMTEXT('GEOMETRYCOLLECTION(MULTIPOINT(-1 2, 0 12), LINESTRING(-2 4, 0 6))')
  UNION ALL
  SELECT ST_GEOGFROMTEXT('GEOMETRYCOLLECTION EMPTY'))
SELECT
  geography AS WKT,
  ST_GEOMETRYTYPE(geography) AS geometry_type_name
FROM example;

/*-------------------------------------------------------------------+-----------------------+
 | WKT                                                               | geometry_type_name    |
 +-------------------------------------------------------------------+-----------------------+
 | POINT(0 1)                                                        | ST_Point              |
 | MULTILINESTRING((2 2, 3 4), (5 6, 7 7))                           | ST_MultiLineString    |
 | GEOMETRYCOLLECTION(MULTIPOINT(-1 2, 0 12), LINESTRING(-2 4, 0 6)) | ST_GeometryCollection |
 | GEOMETRYCOLLECTION EMPTY                                          | ST_GeometryCollection |
 +-------------------------------------------------------------------+-----------------------*/
```

[ogc-link]: https://www.ogc.org/standards/sfa

[wkt-link]: https://en.wikipedia.org/wiki/Well-known_text

[geojson-link]: https://en.wikipedia.org/wiki/GeoJSON

[st-astext]: #st_astext

[st-asgeojson]: #st_asgeojson

## `ST_HAUSDORFFDISTANCE`

```googlesql
ST_HAUSDORFFDISTANCE(
  geography_1,
  geography_2
  [, directed => { TRUE | FALSE } ]
)
```

**Description**

Gets the discrete [Hausdorff distance][h-distance], which is the greatest of all
the distances from a discrete point in one geography to the closest
discrete point in another geography.

**Definitions**

+   `geography_1`: A `GEOGRAPHY` value that represents the first geography.
+   `geography_2`: A `GEOGRAPHY` value that represents the second geography.
+   `directed`: A named argument with a `BOOL` value. Represents the type of
    computation to use on the input geographies. If this argument isn't
    specified, `directed => FALSE` is used by default.

    +   `FALSE`: The largest Hausdorff distance found in
        (`geography_1`, `geography_2`) and
        (`geography_2`, `geography_1`).

    +   `TRUE` (default): The Hausdorff distance for
        (`geography_1`, `geography_2`).

**Details**

If an input geography is `NULL`, the function returns `NULL`.

**Return type**

`DOUBLE`

**Example**

The following query gets the Hausdorff distance between `geo1` and `geo2`:

```googlesql
WITH data AS (
  SELECT
    ST_GEOGFROMTEXT('LINESTRING(20 70, 70 60, 10 70, 70 70)') AS geo1,
    ST_GEOGFROMTEXT('LINESTRING(20 90, 30 90, 60 10, 90 10)') AS geo2
)
SELECT ST_HAUSDORFFDISTANCE(geo1, geo2, directed=>TRUE) AS distance
FROM data;

/*--------------------+
 | distance           |
 +--------------------+
 | 1688933.9832041925 |
 +--------------------*/
```

The following query gets the Hausdorff distance between `geo2` and `geo1`:

```googlesql
WITH data AS (
  SELECT
    ST_GEOGFROMTEXT('LINESTRING(20 70, 70 60, 10 70, 70 70)') AS geo1,
    ST_GEOGFROMTEXT('LINESTRING(20 90, 30 90, 60 10, 90 10)') AS geo2
)
SELECT ST_HAUSDORFFDISTANCE(geo2, geo1, directed=>TRUE) AS distance
FROM data;

/*--------------------+
 | distance           |
 +--------------------+
 | 5802892.745488612  |
 +--------------------*/
```

The following query gets the largest Hausdorff distance between
(`geo1` and `geo2`) and (`geo2` and `geo1`):

```googlesql
WITH data AS (
  SELECT
    ST_GEOGFROMTEXT('LINESTRING(20 70, 70 60, 10 70, 70 70)') AS geo1,
    ST_GEOGFROMTEXT('LINESTRING(20 90, 30 90, 60 10, 90 10)') AS geo2
)
SELECT ST_HAUSDORFFDISTANCE(geo1, geo2, directed=>FALSE) AS distance
FROM data;

/*--------------------+
 | distance           |
 +--------------------+
 | 5802892.745488612  |
 +--------------------*/
```

The following query produces the same results as the previous query because
`ST_HAUSDORFFDISTANCE` uses `directed=>FALSE` by default.

```googlesql
WITH data AS (
  SELECT
    ST_GEOGFROMTEXT('LINESTRING(20 70, 70 60, 10 70, 70 70)') AS geo1,
    ST_GEOGFROMTEXT('LINESTRING(20 90, 30 90, 60 10, 90 10)') AS geo2
)
SELECT ST_HAUSDORFFDISTANCE(geo1, geo2) AS distance
FROM data;
```

[h-distance]: http://en.wikipedia.org/wiki/Hausdorff_distance

## `ST_HAUSDORFFDWITHIN`

```googlesql
ST_HAUSDORFFDWITHIN(
  geography_1,
  geography_2,
  distance
  [, directed => { TRUE | FALSE } ]
)
```

**Description**

Returns `TRUE` if the [Hausdorff distance][st-hausdorffdistance] between `geography_1` and
`geography_2` is less than or equal to the distance given by the
`distance` argument; otherwise, returns `FALSE`.

**Definitions**

+   `geography_1`: A `GEOGRAPHY` value that represents the first geography.
+   `geography_2`: A `GEOGRAPHY` value that represents the second geography.
+   `distance`: A `DOUBLE` value that represents meters on the
    surface of the Earth.
+   `directed`: A named argument with a `BOOL` value. Represents the type of
    computation to use on the input geographies. If this argument isn't
    specified, `directed => FALSE` is used by default.

    +   `FALSE`: The largest Hausdorff distance found in
        (`geography_1`, `geography_2`) and
        (`geography_2`, `geography_1`).

    +   `TRUE` (default): The Hausdorff distance for
        (`geography_1`, `geography_2`).

**Details**

If an input geography is `NULL`, the function returns `NULL`.

**Return type**

`BOOL`

**Examples**

The following example checks whether the Hausdorff distance between the first
and second geographies is less than or equal to 100,000 meters.

```googlesql
SELECT
  ST_HAUSDORFFDWITHIN(
    ST_GEOGFROMTEXT('LINESTRING(10 1, 20 1)'),
    ST_GEOGFROMTEXT('LINESTRING(10 2, 20 2)'),
    100000) AS is_close;

/*----------+
 | is_close |
 +----------+
 | false    |
 +----------*/
```

[st-hausdorffdistance]: #st_hausdorffdistance

## `ST_INTERIORRINGS`

```googlesql
ST_INTERIORRINGS(polygon_geography)
```

**Description**

Returns an array of linestring geographies that corresponds to the interior
rings of a polygon geography. Each interior ring is the border of a hole within
the input polygon.

+   If the input geography is a polygon, excludes the outermost ring of the
    polygon geography and returns the linestrings corresponding to the interior
    rings.
+   If the input is the full `GEOGRAPHY`, returns an empty array.
+   If the input polygon has no holes, returns an empty array.
+   Returns an error if the input isn't a single polygon.

Use the `SAFE` prefix to return `NULL` for invalid input instead of an error.

**Return type**

`ARRAY<LineString GEOGRAPHY>`

**Examples**

```googlesql
WITH geo AS (
  SELECT ST_GEOGFROMTEXT('POLYGON((0 0, 1 1, 1 2, 0 0))') AS g UNION ALL
  SELECT ST_GEOGFROMTEXT('POLYGON((1 1, 1 10, 5 10, 5 1, 1 1), (2 2, 3 4, 2 4, 2 2))') UNION ALL
  SELECT ST_GEOGFROMTEXT('POLYGON((1 1, 1 10, 5 10, 5 1, 1 1), (2 2.5, 3.5 3, 2.5 2, 2 2.5), (3.5 7, 4 6, 3 3, 3.5 7))') UNION ALL
  SELECT ST_GEOGFROMTEXT('fullglobe') UNION ALL
  SELECT NULL)
SELECT ST_INTERIORRINGS(g) AS rings FROM geo;

/*----------------------------------------------------------------------------+
 | rings                                                                      |
 +----------------------------------------------------------------------------+
 | []                                                                         |
 | [LINESTRING(2 2, 3 4, 2 4, 2 2)]                                           |
 | [LINESTRING(2.5 2, 3.5 3, 2 2.5, 2.5 2), LINESTRING(3 3, 4 6, 3.5 7, 3 3)] |
 | []                                                                         |
 | NULL                                                                       |
 +----------------------------------------------------------------------------*/
```

## `ST_INTERSECTION`

```googlesql
ST_INTERSECTION(geography_1, geography_2)
```

**Description**

Returns a `GEOGRAPHY` that represents the point set
intersection of the two input `GEOGRAPHY`s. Thus,
every point in the intersection appears in both `geography_1` and `geography_2`.

If the two input `GEOGRAPHY`s are disjoint, that is,
there are no points that appear in both input `geometry_1` and `geometry_2`,
then an empty `GEOGRAPHY` is returned.

See [ST_INTERSECTS][st-intersects], [ST_DISJOINT][st-disjoint] for related
predicate functions.

**Return type**

`GEOGRAPHY`

[st-intersects]: #st_intersects

[st-disjoint]: #st_disjoint

## `ST_INTERSECTS`

```googlesql
ST_INTERSECTS(geography_1, geography_2)
```

**Description**

Returns `TRUE` if the point set intersection of `geography_1` and `geography_2`
is non-empty. Thus, this function returns `TRUE` if there is at least one point
that appears in both input `GEOGRAPHY`s.

If `ST_INTERSECTS` returns `TRUE`, it implies that [`ST_DISJOINT`][st-disjoint]
returns `FALSE`.

**Return type**

`BOOL`

[st-disjoint]: #st_disjoint

## `ST_INTERSECTSBOX`

```googlesql
ST_INTERSECTSBOX(geography, lng1, lat1, lng2, lat2)
```

**Description**

Returns `TRUE` if `geography` intersects the rectangle between `[lng1, lng2]`
and `[lat1, lat2]`. The edges of the rectangle follow constant lines of
longitude and latitude. `lng1` and `lng2` specify the westmost and eastmost
constant longitude lines that bound the rectangle, and `lat1` and `lat2` specify
the minimum and maximum constant latitude lines that bound the rectangle.

Specify all longitude and latitude arguments in degrees.

**Constraints**

The input arguments are subject to the following constraints:

+   Latitudes should be in the `[-90, 90]` degree range.
+   Longitudes should follow either of the following rules:
    +   Both longitudes are in the `[-180, 180]` degree range.
    +   One of the longitudes is in the `[-180, 180]` degree range, and
        `lng2 - lng1` is in the `[0, 360]` interval.

**Return type**

`BOOL`

**Example**

```googlesql
SELECT p, ST_INTERSECTSBOX(p, -90, 0, 90, 20) AS box1,
       ST_INTERSECTSBOX(p, 90, 0, -90, 20) AS box2
FROM UNNEST([ST_GEOGPOINT(10, 10), ST_GEOGPOINT(170, 10),
             ST_GEOGPOINT(30, 30)]) p

/*----------------+--------------+--------------+
 | p              | box1         | box2         |
 +----------------+--------------+--------------+
 | POINT(10 10)   | TRUE         | FALSE        |
 | POINT(170 10)  | FALSE        | TRUE         |
 | POINT(30 30)   | FALSE        | FALSE        |
 +----------------+--------------+--------------*/
```

## `ST_ISCLOSED`

```googlesql
ST_ISCLOSED(geography_expression)
```

**Description**

Returns `TRUE` for a non-empty Geography, where each element in the Geography
has an empty boundary. The boundary for each element can be defined with
[`ST_BOUNDARY`][st-boundary].

+   A point is closed.
+   A linestring is closed if the start and end points of the linestring are
    the same.
+   A polygon is closed only if it's a full polygon.
+   A collection is closed if and only if every element in the collection is
    closed.

An empty `GEOGRAPHY` isn't closed.

**Return type**

`BOOL`

**Example**

```googlesql
WITH example AS(
  SELECT ST_GEOGFROMTEXT('POINT(5 0)') AS geography
  UNION ALL
  SELECT ST_GEOGFROMTEXT('LINESTRING(0 1, 4 3, 2 6, 0 1)') AS geography
  UNION ALL
  SELECT ST_GEOGFROMTEXT('LINESTRING(2 6, 1 3, 3 9)') AS geography
  UNION ALL
  SELECT ST_GEOGFROMTEXT('GEOMETRYCOLLECTION(POINT(0 0), LINESTRING(1 2, 2 1))') AS geography
  UNION ALL
  SELECT ST_GEOGFROMTEXT('GEOMETRYCOLLECTION EMPTY'))
SELECT
  geography,
  ST_ISCLOSED(geography) AS is_closed,
FROM example;

/*------------------------------------------------------+-----------+
 | geography                                            | is_closed |
 +------------------------------------------------------+-----------+
 | POINT(5 0)                                           | TRUE      |
 | LINESTRING(0 1, 4 3, 2 6, 0 1)                       | TRUE      |
 | LINESTRING(2 6, 1 3, 3 9)                            | FALSE     |
 | GEOMETRYCOLLECTION(POINT(0 0), LINESTRING(1 2, 2 1)) | FALSE     |
 | GEOMETRYCOLLECTION EMPTY                             | FALSE     |
 +------------------------------------------------------+-----------*/
```

[st-boundary]: #st_boundary

## `ST_ISCOLLECTION`

```googlesql
ST_ISCOLLECTION(geography_expression)
```

**Description**

Returns `TRUE` if the total number of points, linestrings, and polygons is
greater than one.

An empty `GEOGRAPHY` isn't a collection.

**Return type**

`BOOL`

## `ST_ISEMPTY`

```googlesql
ST_ISEMPTY(geography_expression)
```

**Description**

Returns `TRUE` if the given `GEOGRAPHY` is empty; that is, the `GEOGRAPHY`
doesn't contain any points, lines, or polygons.

NOTE: An empty `GEOGRAPHY` isn't associated with a particular geometry shape.
For example, the results of expressions `ST_GEOGFROMTEXT('POINT EMPTY')` and
`ST_GEOGFROMTEXT('GEOMETRYCOLLECTION EMPTY')` are identical.

**Return type**

`BOOL`

## `ST_ISRING`

```googlesql
ST_ISRING(geography_expression)
```

**Description**

Returns `TRUE` if the input `GEOGRAPHY` is a linestring and if the
linestring is both [`ST_ISCLOSED`][st-isclosed] and
simple. A linestring is considered simple if it doesn't pass through the
same point twice (with the exception of the start and endpoint, which may
overlap to form a ring).

An empty `GEOGRAPHY` isn't a ring.

**Return type**

`BOOL`

[st-isclosed]: #st_isclosed

## `ST_LENGTH`

```googlesql
ST_LENGTH(geography_expression[, use_spheroid])
```

**Description**

Returns the total length in meters of the lines in the input
`GEOGRAPHY`.

If `geography_expression` is a point or a polygon, returns zero. If
`geography_expression` is a collection, returns the length of the lines in the
collection; if the collection doesn't contain lines, returns zero.

The optional `use_spheroid` parameter determines how this function measures
distance. If `use_spheroid` is `FALSE`, the function measures distance on the
surface of a perfect sphere.

The `use_spheroid` parameter currently only supports
the value `FALSE`. The default value of `use_spheroid` is `FALSE`.

**Return type**

`DOUBLE`

[wgs84-link]: https://en.wikipedia.org/wiki/World_Geodetic_System

## `ST_LINEINTERPOLATEPOINT`

```googlesql
ST_LINEINTERPOLATEPOINT(linestring_geography, fraction)
```

**Description**

Gets a point at a specific fraction in a linestring `GEOGRAPHY` value.

**Definitions**

+  `linestring_geography`: A linestring `GEOGRAPHY` on which the target point
    is located.
+  `fraction`: A `DOUBLE` value that represents a fraction
    along the linestring `GEOGRAPHY` where the target point is located.
    This should be an inclusive value between `0` (start of the
    linestring) and `1` (end of the linestring).

**Details**

+   Returns `NULL` if any input argument is `NULL`.
+   Returns an empty geography if `linestring_geography` is an empty geography.
+   Returns an error if `linestring_geography` isn't a linestring or an empty
    geography, or if `fraction` is outside the `[0, 1]` range.

**Return Type**

`GEOGRAPHY`

**Example**

The following query returns a few points on a linestring. Notice that the
 midpoint of the linestring `LINESTRING(1 1, 5 5)` is slightly different from
 `POINT(3 3)` because the `GEOGRAPHY` type uses geodesic line segments.

```googlesql
WITH fractions AS (
    SELECT 0 AS fraction UNION ALL
    SELECT 0.5 UNION ALL
    SELECT 1 UNION ALL
    SELECT NULL
  )
SELECT
  fraction,
  ST_LINEINTERPOLATEPOINT(ST_GEOGFROMTEXT('LINESTRING(1 1, 5 5)'), fraction)
    AS point
FROM fractions

/*-------------+-------------------------------------------+
 | fraction    | point                                     |
 +-------------+-------------------------------------------+
 | 0           | POINT(1 1)                                |
 | 0.5         | POINT(2.99633827268976 3.00182528336078)  |
 | 1           | POINT(5 5)                                |
 | NULL        | NULL                                      |
 +-------------+-------------------------------------------*/
```

## `ST_LINELOCATEPOINT`

```googlesql
ST_LINELOCATEPOINT(linestring_geography, point_geography)
```

**Description**

Gets a section of a linestring between the start point and a selected point (a
point on the linestring closest to the `point_geography` argument). Returns the
percentage that this section represents in the linestring.

Details:

+   To select a point on the linestring `GEOGRAPHY` (`linestring_geography`),
    this function takes a point `GEOGRAPHY` (`point_geography`) and finds the
    [closest point][st-closestpoint] to it on the linestring.
+   If two points on `linestring_geography` are an equal distance away from
    `point_geography`, it isn't guaranteed which one will be selected.
+   The return value is an inclusive value between 0 and 1 (0-100%).
+   If the selected point is the start point on the linestring, function returns
    0 (0%).
+   If the selected point is the end point on the linestring, function returns 1
    (100%).

`NULL` and error handling:

+   Returns `NULL` if any input argument is `NULL`.
+   Returns an error if `linestring_geography` isn't a linestring or if
    `point_geography` isn't a point. Use the `SAFE` prefix
    to obtain `NULL` for invalid input instead of an error.

**Return Type**

`DOUBLE`

**Examples**

```googlesql
WITH geos AS (
    SELECT ST_GEOGPOINT(0, 0) AS point UNION ALL
    SELECT ST_GEOGPOINT(1, 0) UNION ALL
    SELECT ST_GEOGPOINT(1, 1) UNION ALL
    SELECT ST_GEOGPOINT(2, 2) UNION ALL
    SELECT ST_GEOGPOINT(3, 3) UNION ALL
    SELECT ST_GEOGPOINT(4, 4) UNION ALL
    SELECT ST_GEOGPOINT(5, 5) UNION ALL
    SELECT ST_GEOGPOINT(6, 5) UNION ALL
    SELECT NULL
  )
SELECT
  point AS input_point,
  ST_LINELOCATEPOINT(ST_GEOGFROMTEXT('LINESTRING(1 1, 5 5)'), point)
    AS percentage_from_beginning
FROM geos

/*-------------+---------------------------+
 | input_point | percentage_from_beginning |
 +-------------+---------------------------+
 | POINT(0 0)  | 0                         |
 | POINT(1 0)  | 0                         |
 | POINT(1 1)  | 0                         |
 | POINT(2 2)  | 0.25015214685147907       |
 | POINT(3 3)  | 0.5002284283637185        |
 | POINT(4 4)  | 0.7501905913884388        |
 | POINT(5 5)  | 1                         |
 | POINT(6 5)  | 1                         |
 | NULL        | NULL                      |
 +-------------+---------------------------*/
```

[st-closestpoint]: #st_closestpoint

## `ST_LINESUBSTRING`

```googlesql
ST_LINESUBSTRING(linestring_geography, start_fraction, end_fraction);
```

**Description**

Gets a segment of a linestring at a specific starting and ending fraction.

**Definitions**

+   `linestring_geography`: The LineString `GEOGRAPHY` value that represents the
    linestring from which to extract a segment.
+   `start_fraction`: `DOUBLE` value that represents
    the starting fraction of the total length of `linestring_geography`.
    This must be an inclusive value between 0 and 1 (0-100%).
+   `end_fraction`: `DOUBLE` value that represents
    the ending fraction of the total length of `linestring_geography`.
    This must be an inclusive value between 0 and 1 (0-100%).

**Details**

`end_fraction` must be greater than or equal to `start_fraction`.

If `start_fraction` and `end_fraction` are equal, a linestring with only
one point is produced.

**Return type**

+   LineString `GEOGRAPHY` if the resulting geography has more than one point.
+   Point `GEOGRAPHY` if the resulting geography has only one point.

**Example**

The following query returns the second half of the linestring:

```googlesql
WITH data AS (
  SELECT ST_GEOGFROMTEXT('LINESTRING(20 70, 70 60, 10 70, 70 70)') AS geo1
)
SELECT ST_LINESUBSTRING(geo1, 0.5, 1) AS segment
FROM data;

/*-------------------------------------------------------------+
 | segment                                                     |
 +-------------------------------------------------------------+
 | LINESTRING(49.4760661523471 67.2419539103851, 10 70, 70 70) |
 +-------------------------------------------------------------*/
```

The following query returns a linestring that only contains one point:

```googlesql
WITH data AS (
  SELECT ST_GEOGFROMTEXT('LINESTRING(20 70, 70 60, 10 70, 70 70)') AS geo1
)
SELECT ST_LINESUBSTRING(geo1, 0.5, 0.5) AS segment
FROM data;

/*------------------------------------------+
 | segment                                  |
 +------------------------------------------+
 | POINT(49.4760661523471 67.2419539103851) |
 +------------------------------------------*/
```

## `ST_MAKELINE`

```googlesql
ST_MAKELINE(geography_1, geography_2)
```

```googlesql
ST_MAKELINE(array_of_geography)
```

**Description**

Creates a `GEOGRAPHY` with a single linestring by
concatenating the point or line vertices of each of the input
`GEOGRAPHY`s in the order they are given.

`ST_MAKELINE` comes in two variants. For the first variant, input must be two
`GEOGRAPHY`s. For the second, input must be an `ARRAY` of type `GEOGRAPHY`. In
either variant, each input `GEOGRAPHY` must consist of one of the following
values:

+   Exactly one point.
+   Exactly one linestring.

For the first variant of `ST_MAKELINE`, if either input `GEOGRAPHY` is `NULL`,
`ST_MAKELINE` returns `NULL`. For the second variant, if input `ARRAY` or any
element in the input `ARRAY` is `NULL`, `ST_MAKELINE` returns `NULL`.

**Constraints**

Every edge must span strictly less than 180 degrees.

NOTE: The GoogleSQL snapping process may discard sufficiently short
edges and snap the two endpoints together. For instance, if two input
`GEOGRAPHY`s each contain a point and the two points are separated by a distance
less than the snap radius, the points will be snapped together. In such a case
the result will be a `GEOGRAPHY` with exactly one point.

**Return type**

LineString `GEOGRAPHY`

## `ST_MAKEPOLYGON`

```googlesql
ST_MAKEPOLYGON(polygon_shell[, array_of_polygon_holes])
```

**Description**

Creates a `GEOGRAPHY` containing a single polygon
from linestring inputs, where each input linestring is used to construct a
polygon ring.

`ST_MAKEPOLYGON` comes in two variants. For the first variant, the input
linestring is provided by a single `GEOGRAPHY` containing exactly one
linestring. For the second variant, the input consists of a single `GEOGRAPHY`
and an array of `GEOGRAPHY`s, each containing exactly one linestring.

The first `GEOGRAPHY` in either variant is used to construct the polygon shell.
Additional `GEOGRAPHY`s provided in the input `ARRAY` specify a polygon hole.
For every input `GEOGRAPHY` containing exactly one linestring, the following
must be true:

+   The linestring must consist of at least three distinct vertices.
+   The linestring must be closed: that is, the first and last vertex have to be
    the same. If the first and last vertex differ, the function constructs a
    final edge from the first vertex to the last.

For the first variant of `ST_MAKEPOLYGON`, if either input `GEOGRAPHY` is
`NULL`, `ST_MAKEPOLYGON` returns `NULL`. For the second variant, if
input `ARRAY` or any element in the `ARRAY` is `NULL`, `ST_MAKEPOLYGON` returns
`NULL`.

NOTE: `ST_MAKEPOLYGON` accepts an empty `GEOGRAPHY` as input. `ST_MAKEPOLYGON`
interprets an empty `GEOGRAPHY` as having an empty linestring, which will
create a full loop: that is, a polygon that covers the entire Earth.

**Constraints**

Together, the input rings must form a valid polygon:

+   The polygon shell must cover each of the polygon holes.
+   There can be only one polygon shell (which has to be the first input ring).
    This implies that polygon holes can't be nested.
+   Polygon rings may only intersect in a vertex on the boundary of both rings.

Every edge must span strictly less than 180 degrees.

Each polygon ring divides the sphere into two regions. The first input linesting
to `ST_MAKEPOLYGON` forms the polygon shell, and the interior is chosen to be
the smaller of the two regions. Each subsequent input linestring specifies a
polygon hole, so the interior of the polygon is already well-defined. In order
to define a polygon shell such that the interior of the polygon is the larger of
the two regions, see [`ST_MAKEPOLYGONORIENTED`][st-makepolygonoriented].

NOTE: The GoogleSQL snapping process may discard sufficiently
short edges and snap the two endpoints together. Hence, when vertices are
snapped together, it's possible that a polygon hole that's sufficiently small
may disappear, or the output `GEOGRAPHY` may contain only a line or a
point.

**Return type**

`GEOGRAPHY`

[st-makepolygonoriented]: #st_makepolygonoriented

## `ST_MAKEPOLYGONORIENTED`

```googlesql
ST_MAKEPOLYGONORIENTED(array_of_geography)
```

**Description**

Like `ST_MAKEPOLYGON`, but the vertex ordering of each input linestring
determines the orientation of each polygon ring. The orientation of a polygon
ring defines the interior of the polygon as follows: if someone walks along the
boundary of the polygon in the order of the input vertices, the interior of the
polygon is on the left. This applies for each polygon ring provided.

This variant of the polygon constructor is more flexible since
`ST_MAKEPOLYGONORIENTED` can construct a polygon such that the interior is on
either side of the polygon ring. However, proper orientation of polygon rings is
critical in order to construct the desired polygon.

If the input `ARRAY` or any element in the `ARRAY` is `NULL`,
`ST_MAKEPOLYGONORIENTED` returns `NULL`.

NOTE: The input argument for `ST_MAKEPOLYGONORIENTED` may contain an empty
`GEOGRAPHY`. `ST_MAKEPOLYGONORIENTED` interprets an empty `GEOGRAPHY` as having
an empty linestring, which will create a full loop: that is, a polygon that
covers the entire Earth.

**Constraints**

Together, the input rings must form a valid polygon:

+   The polygon shell must cover each of the polygon holes.
+   There must be only one polygon shell, which must to be the first input ring.
    This implies that polygon holes can't be nested.
+   Polygon rings may only intersect in a vertex on the boundary of both rings.

Every edge must span strictly less than 180 degrees.

`ST_MAKEPOLYGONORIENTED` relies on the ordering of the input vertices of each
linestring to determine the orientation of the polygon. This applies to the
polygon shell and any polygon holes. `ST_MAKEPOLYGONORIENTED` expects all
polygon holes to have the opposite orientation of the shell. See
[`ST_MAKEPOLYGON`][st-makepolygon] for an alternate polygon constructor, and
other constraints on building a valid polygon.

NOTE: Due to the GoogleSQL snapping process, edges with a sufficiently
short length will be discarded and the two endpoints will be snapped to a single
point. Therefore, it's possible that vertices in a linestring may be snapped
together such that one or more edge disappears. Hence, it's possible that a
polygon hole that's sufficiently small may disappear, or the resulting
`GEOGRAPHY` may contain only a line or a point.

**Return type**

`GEOGRAPHY`

[st-makepolygon]: #st_makepolygon

## `ST_MAXDISTANCE`

```googlesql
ST_MAXDISTANCE(geography_1, geography_2[, use_spheroid])
```

Returns the longest distance in meters between two non-empty
`GEOGRAPHY`s; that is, the distance between two
vertices where the first vertex is in the first
`GEOGRAPHY`, and the second vertex is in the second
`GEOGRAPHY`. If `geography_1` and `geography_2` are the
same `GEOGRAPHY`, the function returns the distance
between the two most distant vertices in that
`GEOGRAPHY`.

If either of the input `GEOGRAPHY`s is empty,
`ST_MAXDISTANCE` returns `NULL`.

The optional `use_spheroid` parameter determines how this function measures
distance. If `use_spheroid` is `FALSE`, the function measures distance on the
surface of a perfect sphere.

The `use_spheroid` parameter currently only supports
the value `FALSE`. The default value of `use_spheroid` is `FALSE`.

**Return type**

`DOUBLE`

[wgs84-link]: https://en.wikipedia.org/wiki/World_Geodetic_System

## `ST_NPOINTS`

```googlesql
ST_NPOINTS(geography_expression)
```

**Description**

An alias of [ST_NUMPOINTS][st-numpoints].

[st-numpoints]: #st_numpoints

## `ST_NUMGEOMETRIES`

```
ST_NUMGEOMETRIES(geography_expression)
```

**Description**

Returns the number of geometries in the input `GEOGRAPHY`. For a single point,
linestring, or polygon, `ST_NUMGEOMETRIES` returns `1`. For any collection of
geometries, `ST_NUMGEOMETRIES` returns the number of geometries making up the
collection. `ST_NUMGEOMETRIES` returns `0` if the input is the empty
`GEOGRAPHY`.

**Return type**

`INT64`

**Example**

The following example computes `ST_NUMGEOMETRIES` for a single point geography,
two collections, and an empty geography.

```googlesql
WITH example AS(
  SELECT ST_GEOGFROMTEXT('POINT(5 0)') AS geography
  UNION ALL
  SELECT ST_GEOGFROMTEXT('MULTIPOINT(0 1, 4 3, 2 6)') AS geography
  UNION ALL
  SELECT ST_GEOGFROMTEXT('GEOMETRYCOLLECTION(POINT(0 0), LINESTRING(1 2, 2 1))') AS geography
  UNION ALL
  SELECT ST_GEOGFROMTEXT('GEOMETRYCOLLECTION EMPTY'))
SELECT
  geography,
  ST_NUMGEOMETRIES(geography) AS num_geometries,
FROM example;

/*------------------------------------------------------+----------------+
 | geography                                            | num_geometries |
 +------------------------------------------------------+----------------+
 | POINT(5 0)                                           | 1              |
 | MULTIPOINT(0 1, 4 3, 2 6)                            | 3              |
 | GEOMETRYCOLLECTION(POINT(0 0), LINESTRING(1 2, 2 1)) | 2              |
 | GEOMETRYCOLLECTION EMPTY                             | 0              |
 +------------------------------------------------------+----------------*/
```

## `ST_NUMPOINTS`

```googlesql
ST_NUMPOINTS(geography_expression)
```

**Description**

Returns the number of vertices in the input
`GEOGRAPHY`. This includes the number of points, the
number of linestring vertices, and the number of polygon vertices.

NOTE: The first and last vertex of a polygon ring are counted as distinct
vertices.

**Return type**

`INT64`

## `ST_PERIMETER`

```googlesql
ST_PERIMETER(geography_expression[, use_spheroid])
```

**Description**

Returns the length in meters of the boundary of the polygons in the input
`GEOGRAPHY`.

If `geography_expression` is a point or a line, returns zero. If
`geography_expression` is a collection, returns the perimeter of the polygons
in the collection; if the collection doesn't contain polygons, returns zero.

The optional `use_spheroid` parameter determines how this function measures
distance. If `use_spheroid` is `FALSE`, the function measures distance on the
surface of a perfect sphere.

The `use_spheroid` parameter currently only supports
the value `FALSE`. The default value of `use_spheroid` is `FALSE`.

**Return type**

`DOUBLE`

[wgs84-link]: https://en.wikipedia.org/wiki/World_Geodetic_System

## `ST_POINTN`

```googlesql
ST_POINTN(linestring_geography, index)
```

**Description**

Returns the Nth point of a linestring geography as a point geography, where N is
the index. The index is 1-based. Negative values are counted backwards from the
end of the linestring, so that -1 is the last point. Returns an error if the
input isn't a linestring, if the input is empty, or if there is no vertex at
the given index. Use the `SAFE` prefix to obtain `NULL` for invalid input
instead of an error.

**Return Type**

Point `GEOGRAPHY`

**Example**

The following example uses `ST_POINTN`, [`ST_STARTPOINT`][st-startpoint] and
[`ST_ENDPOINT`][st-endpoint] to extract points from a linestring.

```googlesql
WITH linestring AS (
    SELECT ST_GEOGFROMTEXT('LINESTRING(1 1, 2 1, 3 2, 3 3)') g
)
SELECT ST_POINTN(g, 1) AS first, ST_POINTN(g, -1) AS last,
    ST_POINTN(g, 2) AS second, ST_POINTN(g, -2) AS second_to_last
FROM linestring;

/*--------------+--------------+--------------+----------------+
 | first        | last         | second       | second_to_last |
 +--------------+--------------+--------------+----------------+
 | POINT(1 1)   | POINT(3 3)   | POINT(2 1)   | POINT(3 2)     |
 +--------------+--------------+--------------+----------------*/
```

[st-startpoint]: #st_startpoint

[st-endpoint]: #st_endpoint

## `ST_SIMPLIFY`

```googlesql
ST_SIMPLIFY(geography, tolerance_meters)
```

**Description**

Returns a simplified version of `geography`, the given input
`GEOGRAPHY`. The input `GEOGRAPHY` is simplified by replacing nearly straight
chains of short edges with a single long edge. The input `geography` will not
change by more than the tolerance specified by `tolerance_meters`. Thus,
simplified edges are guaranteed to pass within `tolerance_meters` of the
*original* positions of all vertices that were removed from that edge. The given
`tolerance_meters` is in meters on the surface of the Earth.

Note that `ST_SIMPLIFY` preserves topological relationships, which means that
no new crossing edges will be created and the output will be valid. For a large
enough tolerance, adjacent shapes may collapse into a single object, or a shape
could be simplified to a shape with a smaller dimension.

**Constraints**

For `ST_SIMPLIFY` to have any effect, `tolerance_meters` must be non-zero.

`ST_SIMPLIFY` returns an error if the tolerance specified by `tolerance_meters`
is one of the following:

+ A negative tolerance.
+ Greater than ~7800 kilometers.

**Return type**

`GEOGRAPHY`

**Examples**

The following example shows how `ST_SIMPLIFY` simplifies the input line
`GEOGRAPHY` by removing intermediate vertices.

```googlesql
WITH example AS
 (SELECT ST_GEOGFROMTEXT('LINESTRING(0 0, 0.05 0, 0.1 0, 0.15 0, 2 0)') AS line)
SELECT
   line AS original_line,
   ST_SIMPLIFY(line, 1) AS simplified_line
FROM example;

/*---------------------------------------------+----------------------+
 |                original_line                |   simplified_line    |
 +---------------------------------------------+----------------------+
 | LINESTRING(0 0, 0.05 0, 0.1 0, 0.15 0, 2 0) | LINESTRING(0 0, 2 0) |
 +---------------------------------------------+----------------------*/
```

The following example illustrates how the result of `ST_SIMPLIFY` can have a
lower dimension than the original shape.

```googlesql
WITH example AS
 (SELECT
    ST_GEOGFROMTEXT('POLYGON((0 0, 0.1 0, 0.1 0.1, 0 0))') AS polygon,
    t AS tolerance
  FROM UNNEST([1000, 10000, 100000]) AS t)
SELECT
  polygon AS original_triangle,
  tolerance AS tolerance_meters,
  ST_SIMPLIFY(polygon, tolerance) AS simplified_result
FROM example

/*-------------------------------------+------------------+-------------------------------------+
 |          original_triangle          | tolerance_meters |          simplified_result          |
 +-------------------------------------+------------------+-------------------------------------+
 | POLYGON((0 0, 0.1 0, 0.1 0.1, 0 0)) |             1000 | POLYGON((0 0, 0.1 0, 0.1 0.1, 0 0)) |
 | POLYGON((0 0, 0.1 0, 0.1 0.1, 0 0)) |            10000 |            LINESTRING(0 0, 0.1 0.1) |
 | POLYGON((0 0, 0.1 0, 0.1 0.1, 0 0)) |           100000 |                          POINT(0 0) |
 +-------------------------------------+------------------+-------------------------------------*/
```

## `ST_SNAPTOGRID`

```googlesql
ST_SNAPTOGRID(geography_expression, grid_size)
```

**Description**

Returns the input `GEOGRAPHY`, where each vertex has
been snapped to a longitude/latitude grid. The grid size is determined by the
`grid_size` parameter which is given in degrees.

**Constraints**

Arbitrary grid sizes aren't supported. The `grid_size` parameter is rounded so
that it's of the form `10^n`, where `-10 < n < 0`.

**Return type**

`GEOGRAPHY`

## `ST_STARTPOINT`

```googlesql
ST_STARTPOINT(linestring_geography)
```

**Description**

Returns the first point of a linestring geography as a point geography. Returns
an error if the input isn't a linestring or if the input is empty. Use the
`SAFE` prefix to obtain `NULL` for invalid input instead of an error.

**Return Type**

Point `GEOGRAPHY`

**Example**

```googlesql
SELECT ST_STARTPOINT(ST_GEOGFROMTEXT('LINESTRING(1 1, 2 1, 3 2, 3 3)')) first

/*--------------+
 | first        |
 +--------------+
 | POINT(1 1)   |
 +--------------*/
```

## `ST_TOUCHES`

```googlesql
ST_TOUCHES(geography_1, geography_2)
```

**Description**

Returns `TRUE` provided the following two conditions are satisfied:

1.  `geography_1` intersects `geography_2`.
1.  The interior of `geography_1` and the interior of `geography_2` are
    disjoint.

**Return type**

`BOOL`

## `ST_UNION`

```googlesql
ST_UNION(geography_1, geography_2)
```

```googlesql
ST_UNION(array_of_geography)
```

**Description**

Returns a `GEOGRAPHY` that represents the point set
union of all input `GEOGRAPHY`s.

`ST_UNION` comes in two variants. For the first variant, input must be two
`GEOGRAPHY`s. For the second, the input is an
`ARRAY` of type `GEOGRAPHY`.

For the first variant of `ST_UNION`, if an input
`GEOGRAPHY` is `NULL`, `ST_UNION` returns `NULL`.
For the second variant, if the input `ARRAY` value
is `NULL`, `ST_UNION` returns `NULL`.
For a non-`NULL` input `ARRAY`, the union is computed
and `NULL` elements are ignored so that they don't affect the output.

See [`ST_UNION_AGG`][st-union-agg] for the aggregate version of `ST_UNION`.

**Return type**

`GEOGRAPHY`

**Example**

```googlesql
SELECT ST_UNION(
  ST_GEOGFROMTEXT('LINESTRING(-122.12 47.67, -122.19 47.69)'),
  ST_GEOGFROMTEXT('LINESTRING(-122.12 47.67, -100.19 47.69)')
) AS results

/*---------------------------------------------------------+
 | results                                                 |
 +---------------------------------------------------------+
 | LINESTRING(-100.19 47.69, -122.12 47.67, -122.19 47.69) |
 +---------------------------------------------------------*/
```

[st-union-agg]: #st_union_agg

## `ST_UNION_AGG`

```googlesql
ST_UNION_AGG(geography)
```

**Description**

Returns a `GEOGRAPHY` that represents the point set
union of all input `GEOGRAPHY`s.

`ST_UNION_AGG` ignores `NULL` input `GEOGRAPHY` values.

See [`ST_UNION`][st-union] for the non-aggregate version of `ST_UNION_AGG`.

**Return type**

`GEOGRAPHY`

**Example**

```googlesql
SELECT ST_UNION_AGG(items) AS results
FROM UNNEST([
  ST_GEOGFROMTEXT('LINESTRING(-122.12 47.67, -122.19 47.69)'),
  ST_GEOGFROMTEXT('LINESTRING(-122.12 47.67, -100.19 47.69)'),
  ST_GEOGFROMTEXT('LINESTRING(-122.12 47.67, -122.19 47.69)')]) as items;

/*---------------------------------------------------------+
 | results                                                 |
 +---------------------------------------------------------+
 | LINESTRING(-100.19 47.69, -122.12 47.67, -122.19 47.69) |
 +---------------------------------------------------------*/
```

[st-union]: #st_union

## `ST_WITHIN`

```googlesql
ST_WITHIN(geography_1, geography_2)
```

**Description**

Returns `TRUE` if no point of `geography_1` is outside of `geography_2` and
the interiors of `geography_1` and `geography_2` intersect.

Given two geographies `a` and `b`, `ST_WITHIN(a, b)` returns the same result
as [`ST_CONTAINS`][st-contains]`(b, a)`. Note the opposite order of arguments.

**Return type**

`BOOL`

[st-contains]: #st_contains

## `ST_X`

```googlesql
ST_X(point_geography_expression)
```

**Description**

Returns the longitude in degrees of the single-point input
`GEOGRAPHY`.

For any input `GEOGRAPHY` that isn't a single point,
including an empty `GEOGRAPHY`, `ST_X` returns an
error. Use the `SAFE.` prefix to obtain `NULL`.

**Return type**

`DOUBLE`

**Example**

The following example uses `ST_X` and `ST_Y` to extract coordinates from
single-point geographies.

```googlesql
WITH points AS
   (SELECT ST_GEOGPOINT(i, i + 1) AS p FROM UNNEST([0, 5, 12]) AS i)
 SELECT
   p,
   ST_X(p) as longitude,
   ST_Y(p) as latitude
FROM points;

/*--------------+-----------+----------+
 | p            | longitude | latitude |
 +--------------+-----------+----------+
 | POINT(0 1)   | 0.0       | 1.0      |
 | POINT(5 6)   | 5.0       | 6.0      |
 | POINT(12 13) | 12.0      | 13.0     |
 +--------------+-----------+----------*/
```

## `ST_Y`

```googlesql
ST_Y(point_geography_expression)
```

**Description**

Returns the latitude in degrees of the single-point input
`GEOGRAPHY`.

For any input `GEOGRAPHY` that isn't a single point,
including an empty `GEOGRAPHY`, `ST_Y` returns an
error. Use the `SAFE.` prefix to return `NULL` instead.

**Return type**

`DOUBLE`

**Example**

See [`ST_X`][st-x] for example usage.

[st-x]: #st_x

