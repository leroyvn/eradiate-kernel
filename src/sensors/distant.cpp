#include <mitsuba/core/bbox.h>
#include <mitsuba/core/bsphere.h>
#include <mitsuba/core/math.h>
#include <mitsuba/core/properties.h>
#include <mitsuba/core/transform.h>
#include <mitsuba/core/warp.h>
#include <mitsuba/render/scene.h>
#include <mitsuba/render/sensor.h>
#include <mitsuba/render/shape.h>

NAMESPACE_BEGIN(mitsuba)

enum class RayTargetType { Shape, Point, None };
enum class RayOriginType { Shape, BoundingSphere };

// Forward declaration of specialized DistantSensor
template <typename Float, typename Spectrum, RayTargetType TargetType,
          RayOriginType OriginType>
class DistantSensorImpl;

/**!

.. _sensor-distant:

Distant directional sensor (:monosp:`distant`)
----------------------------------------------

.. pluginparameters::

 * - to_world
   - |transform|
   - Sensor-to-world transformation matrix.
 * - direction
   - |vector|
   - Alternative (and exclusive) to `to_world`. Direction from which the
     sensor will be recording in world coordinates.
 * - ray_target
   - |point| or nested :paramtype:`shape` plugin
   - *Optional.* Define the ray target sampling strategy.
     If this parameter is unset, ray target points are sampled uniformly on
     the cross section of the scene's bounding sphere.
     If a |point| is passed, rays will target it.
     If a shape plugin is passed, ray target points will be sampled from its
     surface.
 * - ray_origin
   - nested :paramtype:`shape` plugin
   - *Optional.* Specify the ray origin computation strategy.
     If this parameter is unset, ray origins will be positioned using the
     bounding sphere of the scene so as to ensure that they lie outside of any
     geometry.
     If a shape plugin is passed, ray origin points will be positioned by
     projecting the sampled target point onto the shape following the sampled
     ray direction. If the projection is impossible, an invalid ray is returned
     with zero weights. *Note*: ray invalidation occurs per-lane in packet
     modes.

This sensor plugin implements a distant directional sensor which records
radiation leaving the scene in a given direction. By default, it records the
(spectral) radiant flux per unit solid angle leaving the scene in the specified
direction (in unit power per unit solid angle).
Rays target points are sampled from the cross section of the scene's bounding
sphere and their origins are positioned outside of the scene's geometry.

Rays sampled from this sensor can be tuned so as to target a specific region of
the scene using the ``ray_target`` parameter. The recorded flux is proportional 
to the surface area of the target shape. If the target is a point, the sensor
records a radiance.

The positioning of the origin of those rays can also be controlled using the
``ray_origin``. This is particularly useful when the scene has a dimension much
small than the others and it is not necessary that ray origins are located at
the scene's bounding sphere.

.. warning::

   If this sensor is used with its default targeting and origin computation
   strategies and an environment map emitter, it will also record radiant flux
   coming from the part of emitter appearing through the scene's bounding sphere
   cross section. Care should be taken notably when using the ``constant`` or
   ``envmap`` emitters.
*/

template <typename Float, typename Spectrum>
class DistantSensor final : public Sensor<Float, Spectrum> {
public:
    MTS_IMPORT_BASE(Sensor, m_world_transform, m_film)
    MTS_IMPORT_TYPES(Shape)

    DistantSensor(const Properties &props) : Base(props), m_props(props) {

        // Get target
        if (props.has_property("ray_target")) {
            try {
                // We first try to get a point
                props.point3f("ray_target");
                m_ray_target_type = RayTargetType::Point;
            } catch (const std::runtime_error &e) {
                // If it fails, we assume it's a shape
                m_ray_target_type = RayTargetType::Shape;
            }
        } else {
            m_ray_target_type = RayTargetType::None;
        }

        // Get origin
        if (props.has_property("ray_origin"))
            m_ray_origin_type = RayOriginType::Shape;
        else {
            m_ray_origin_type = RayOriginType::BoundingSphere;
        }

        props.mark_queried("direction");
        props.mark_queried("to_world");
        props.mark_queried("ray_target");
        props.mark_queried("ray_origin");
    }

    // This sensor does not occupy any particular region of space, return an
    // invalid bounding box
    ScalarBoundingBox3f bbox() const override { return ScalarBoundingBox3f(); }

    template <RayTargetType TargetType, RayOriginType OriginType>
    using Impl = DistantSensorImpl<Float, Spectrum, TargetType, OriginType>;

    // Recursively expand into an implementation specialized to the ray origin
    // specification.
    std::vector<ref<Object>> expand() const override {
        ref<Object> result;
        switch (m_ray_target_type) {
            case RayTargetType::Shape:
                switch (m_ray_origin_type) {
                    case RayOriginType::BoundingSphere:
                        result =
                            (Object *) new Impl<RayTargetType::Shape,
                                                RayOriginType::BoundingSphere>(
                                m_props);
                        break;
                    case RayOriginType::Shape:
                        result =
                            (Object *) new Impl<RayTargetType::Shape,
                                                RayOriginType::Shape>(m_props);
                        break;
                    default:
                        Throw("Unsupported ray origin type!");
                }
                break;
            case RayTargetType::Point:
                switch (m_ray_origin_type) {
                    case RayOriginType::BoundingSphere:
                        result =
                            (Object *) new Impl<RayTargetType::Point,
                                                RayOriginType::BoundingSphere>(
                                m_props);
                        break;
                    case RayOriginType::Shape:
                        result =
                            (Object *) new Impl<RayTargetType::Point,
                                                RayOriginType::Shape>(m_props);
                        break;
                    default:
                        Throw("Unsupported ray origin type!");
                }
                break;
            case RayTargetType::None:
                switch (m_ray_origin_type) {
                    case RayOriginType::BoundingSphere:
                        result =
                            (Object *) new Impl<RayTargetType::None,
                                                RayOriginType::BoundingSphere>(
                                m_props);
                        break;
                    case RayOriginType::Shape:
                        result =
                            (Object *) new Impl<RayTargetType::None,
                                                RayOriginType::Shape>(m_props);
                        break;
                    default:
                        Throw("Unsupported ray origin type!");
                }
                break;
            default:
                Throw("Unsupported ray target type!");
        }
        return { result };
    }

    MTS_DECLARE_CLASS()

protected:
    Properties m_props;
    RayTargetType m_ray_target_type;
    RayOriginType m_ray_origin_type;
};

template <typename Float, typename Spectrum, RayTargetType TargetType,
          RayOriginType OriginType>
class DistantSensorImpl final : public Sensor<Float, Spectrum> {
public:
    MTS_IMPORT_BASE(Sensor, m_world_transform, m_film)
    MTS_IMPORT_TYPES(Scene, Shape)

    DistantSensorImpl(const Properties &props) : Base(props) {

        // Compute transform, possibly based on direction parameter
        if (props.has_property("direction")) {
            if (props.has_property("to_world")) {
                Throw("Only one of the parameters 'direction' and 'to_world'"
                      "can be specified at the same time!'");
            }

            ScalarVector3f direction(normalize(props.vector3f("direction")));
            auto [up, unused] = coordinate_system(direction);

            m_world_transform =
                new AnimatedTransform(ScalarTransform4f::look_at(
                    ScalarPoint3f(0.0f), ScalarPoint3f(direction), up));
        }

        // Set ray target if relevant
        if constexpr (TargetType == RayTargetType::Point) {
            m_ray_target_point = props.point3f("ray_target");
        } else if constexpr (TargetType == RayTargetType::Shape) {
            auto obj           = props.object("ray_target");
            m_ray_target_shape = dynamic_cast<Shape *>(obj.get());

            if (!m_ray_target_shape)
                Throw("Invalid parameter ray_target, must be a Point3f or a "
                      "Shape.");
        } else {
            Log(Debug, "No target specified.");
        }

        // Set ray origin
        if constexpr (OriginType == RayOriginType::Shape) {
            auto obj           = props.object("ray_origin");
            m_ray_origin_shape = dynamic_cast<Shape *>(obj.get());

            if (!m_ray_origin_shape)
                Throw("Invalid parameter ray_origin, must be a Shape.");
        } else {
            Log(Debug, "Using bounding sphere for ray origins.");
        }

        // Check film size
        if (m_film->size() != ScalarPoint2i(1, 1))
            Throw("This sensor only supports films of size 1x1 Pixels!");

        if (m_film->reconstruction_filter()->radius() >
            0.5f + math::RayEpsilon<Float>)
            Log(Warn, "This sensor should be used with a reconstruction filter "
                      "with a radius of 0.5 or lower (e.g. default box)");
    }

    void set_scene(const Scene *scene) override {
        m_bsphere = scene->bbox().bounding_sphere();
        m_bsphere.radius =
            max(math::RayEpsilon<Float>,
                m_bsphere.radius * (1.f + math::RayEpsilon<Float>) );
    }

    template <typename RayVariant>
    std::pair<RayVariant, Spectrum>
    sample_ray_impl(Float time, Float wavelength_sample,
                    const Point2f & /*film_sample*/,
                    const Point2f &aperture_sample, Mask active) const {
        MTS_MASK_ARGUMENT(active);

        RayVariant ray;
        ray.time = time;

        // 1. Sample spectrum
        auto [wavelengths, wav_weight] =
            sample_wavelength<Float, Spectrum>(wavelength_sample);
        ray.wavelengths = wavelengths;

        // 2. Set ray direction
        auto trafo = m_world_transform->eval(time, active);
        ray.d      = trafo.transform_affine(Vector3f{ 0.f, 0.f, 1.f });

        // 3. Sample ray origin
        Spectrum ray_weight = 0.f;
        Point3f ray_target  = m_ray_target_point;

        // 3.1. Sample target point
        if constexpr (TargetType == RayTargetType::Point) {
            // Target point selection already handled during init
            ray_weight = wav_weight * Frame3f::cos_theta(-ray.d);
            // Warning: to be checked for non-horizontal surfaces
        }

        else if constexpr (TargetType == RayTargetType::Shape) {
            // Use area-based sampling of shape
            PositionSample3f ps = m_ray_target_shape->sample_position(
                time, aperture_sample, active);
            SurfaceInteraction3f si(ps, zero<Wavelength>());
            ray_target = si.p;
            ray_weight = wav_weight * dot(-ray.d, si.n) / ps.pdf;
        }

        else { // if constexpr (TargetType == RayTargetType::None) {
            // Sample target uniformly on bounding sphere cross section
            Point2f offset =
                warp::square_to_uniform_disk_concentric(aperture_sample);
            Vector3f perp_offset =
                trafo.transform_affine(Vector3f{ offset.x(), offset.y(), 0.f });
            ray_target = m_bsphere.center + perp_offset * m_bsphere.radius;
            ray_weight = wav_weight * (math::Pi<Float> * sqr(m_bsphere.radius));
        }

        // 3.2. Determine origin point
        if constexpr (OriginType == RayOriginType::Shape) {
            // Project target onto origin shape following ray direction
            Ray3f tmp_ray(ray_target, -ray.d, time);
            SurfaceInteraction3f si = m_ray_origin_shape->ray_intersect(
                tmp_ray, HitComputeFlags::Minimal, active);
            active &= si.is_valid();
            ray.o = si.p;
        }

        else { // if constexpr (OriginType == OriginType::BoundingSphere) {
            // Use the scene's bounding sphere to safely position ray origin
            if constexpr (TargetType == RayTargetType::None)
                ray.o = ray_target - ray.d * m_bsphere.radius;
            else
                ray.o = ray_target - ray.d * 2.f * m_bsphere.radius;
        }

        return { ray, ray_weight && active };
    }

    std::pair<Ray3f, Spectrum> sample_ray(Float time, Float wavelength_sample,
                                          const Point2f &film_sample,
                                          const Point2f &aperture_sample,
                                          Mask active) const override {
        MTS_MASKED_FUNCTION(ProfilerPhase::EndpointSampleRay, active);

        auto [ray, ray_weight] = sample_ray_impl<Ray3f>(
            time, wavelength_sample, film_sample, aperture_sample, active);
        ray.update();
        return { ray, ray_weight && active };
    }

    std::pair<RayDifferential3f, Spectrum> sample_ray_differential(
        Float time, Float wavelength_sample, const Point2f &film_sample,
        const Point2f &aperture_sample, Mask active) const override {
        MTS_MASKED_FUNCTION(ProfilerPhase::EndpointSampleRay, active);

        auto [ray, ray_weight] = sample_ray_impl<RayDifferential3f>(
            time, wavelength_sample, film_sample, aperture_sample, active);

        // Set differentials; since the film size is always 1x1, we don't
        // have differentials
        ray.has_differentials = false;

        ray.update();
        return { ray, ray_weight && active };
    }

    // This sensor does not occupy any particular region of space, return an
    // invalid bounding box
    ScalarBoundingBox3f bbox() const override { return ScalarBoundingBox3f(); }

    std::string to_string() const override {
        std::ostringstream oss;
        oss << "DistantSensor[" << std::endl
            << "  world_transform = " << m_world_transform << "," << std::endl
            << "  film = " << m_film << "," << std::endl;

        if constexpr (TargetType == RayTargetType::Point)
            oss << "  ray_target = " << m_ray_target_point << std::endl;
        else if constexpr (TargetType == RayTargetType::Shape)
            oss << "  ray_target = " << m_ray_target_shape << std::endl;
        else // if constexpr (TargetType == RayTargetType::None)
            oss << "  ray_target = none" << std::endl;

        if constexpr (OriginType == RayOriginType::Shape)
            oss << "  ray_origin = " << m_ray_origin_shape << std::endl;
        else // if constexpr (OriginType == RayOriginType::BoundingSphere)
            oss << "  ray_origin = bounding_sphere" << std::endl;

        oss << "]";

        return oss.str();
    }

    MTS_DECLARE_CLASS()

protected:
    ScalarBoundingSphere3f m_bsphere;

    ref<Shape> m_ray_target_shape;
    Point3f m_ray_target_point;

    ref<Shape> m_ray_origin_shape;
};

MTS_IMPLEMENT_CLASS_VARIANT(DistantSensor, Sensor)
MTS_EXPORT_PLUGIN(DistantSensor, "DistantSensor")

NAMESPACE_BEGIN(detail)
template <RayTargetType TargetType, RayOriginType OriginType>
constexpr const char *distant_sensor_class_name() {
    if constexpr (TargetType == RayTargetType::Shape) {
        if constexpr (OriginType == RayOriginType::Shape)
            return "DistantSensor_Shape_Shape";
        else if constexpr (OriginType == RayOriginType::BoundingSphere)
            return "DistantSensor_Shape_BoundingSphere";
    } else if constexpr (TargetType == RayTargetType::Point) {
        if constexpr (OriginType == RayOriginType::Shape)
            return "DistantSensor_Point_Shape";
        else if constexpr (OriginType == RayOriginType::BoundingSphere)
            return "DistantSensor_Point_BoundingSphere";
    } else if constexpr (TargetType == RayTargetType::None) {
        if constexpr (OriginType == RayOriginType::Shape)
            return "DistantSensor_None_Shape";
        else if constexpr (OriginType == RayOriginType::BoundingSphere)
            return "DistantSensor_None_BoundingSphere";
    }
}
NAMESPACE_END(detail)

template <typename Float, typename Spectrum, RayTargetType TargetType,
          RayOriginType OriginType>
Class *DistantSensorImpl<Float, Spectrum, TargetType, OriginType>::m_class =
    new Class(detail::distant_sensor_class_name<TargetType, OriginType>(),
              "Sensor", ::mitsuba::detail::get_variant<Float, Spectrum>(),
              nullptr, nullptr);

template <typename Float, typename Spectrum, RayTargetType TargetType,
          RayOriginType OriginType>
const Class *
DistantSensorImpl<Float, Spectrum, TargetType, OriginType>::class_() const {
    return m_class;
}

NAMESPACE_END(mitsuba)