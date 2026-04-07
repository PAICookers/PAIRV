/*
 *  This file is generated with Embedded Proto, PLEASE DO NOT EDIT!
 *  source: test.proto
 */

// This file is generated. Please do not edit!
#ifndef TEST_H
#define TEST_H

#include <cstdint>
#include <MessageInterface.h>
#include <WireFormatter.h>
#include <Fields.h>
#include <MessageSizeCalculator.h>
#include <ReadBufferSection.h>
#include <RepeatedFieldFixedSize.h>
#include <FieldStringBytes.h>
#include <Errors.h>
#include <Defines.h>
#include <limits>

// Include external proto definitions


enum class OutputCodec : uint32_t
{
  OUTPUT_CODEC_UNSET = 0,
  DIRECT_AXON = 1,
  PACKED_32BIT = 2
};

class RouteSpan final: public ::EmbeddedProto::MessageInterface
{
  public:
    RouteSpan() = default;
    RouteSpan(const RouteSpan& rhs )
    {
      set_tick_relative(rhs.get_tick_relative());
      set_addr_axon(rhs.get_addr_axon());
    }

    RouteSpan(const RouteSpan&& rhs ) noexcept
    {
      set_tick_relative(rhs.get_tick_relative());
      set_addr_axon(rhs.get_addr_axon());
    }

    ~RouteSpan() override = default;

    enum class FieldNumber : uint32_t
    {
      NOT_SET = 0,
      TICK_RELATIVE = 1,
      ADDR_AXON = 2
    };

    RouteSpan& operator=(const RouteSpan& rhs)
    {
      set_tick_relative(rhs.get_tick_relative());
      set_addr_axon(rhs.get_addr_axon());
      return *this;
    }

    RouteSpan& operator=(const RouteSpan&& rhs) noexcept
    {
      set_tick_relative(rhs.get_tick_relative());
      set_addr_axon(rhs.get_addr_axon());
      return *this;
    }

    static constexpr char const* TICK_RELATIVE_NAME = "tick_relative";
    inline void clear_tick_relative() { tick_relative_.clear(); }
    inline void set_tick_relative(const uint32_t& value) { tick_relative_ = value; }
    inline void set_tick_relative(const uint32_t&& value) { tick_relative_ = value; }
    inline uint32_t& mutable_tick_relative() { return tick_relative_.get(); }
    inline const uint32_t& get_tick_relative() const { return tick_relative_.get(); }
    inline uint32_t tick_relative() const { return tick_relative_.get(); }

    static constexpr char const* ADDR_AXON_NAME = "addr_axon";
    inline void clear_addr_axon() { addr_axon_.clear(); }
    inline void set_addr_axon(const uint32_t& value) { addr_axon_ = value; }
    inline void set_addr_axon(const uint32_t&& value) { addr_axon_ = value; }
    inline uint32_t& mutable_addr_axon() { return addr_axon_.get(); }
    inline const uint32_t& get_addr_axon() const { return addr_axon_.get(); }
    inline uint32_t addr_axon() const { return addr_axon_.get(); }


    ::EmbeddedProto::Error serialize(::EmbeddedProto::WriteBufferInterface& buffer) const override
    {
      ::EmbeddedProto::Error return_value = ::EmbeddedProto::Error::NO_ERRORS;

      if((0U != tick_relative_.get()) && (::EmbeddedProto::Error::NO_ERRORS == return_value))
      {
        return_value = tick_relative_.serialize_with_id(static_cast<uint32_t>(FieldNumber::TICK_RELATIVE), buffer, false);
      }

      if((0U != addr_axon_.get()) && (::EmbeddedProto::Error::NO_ERRORS == return_value))
      {
        return_value = addr_axon_.serialize_with_id(static_cast<uint32_t>(FieldNumber::ADDR_AXON), buffer, false);
      }

      return return_value;
    };

    ::EmbeddedProto::Error deserialize(::EmbeddedProto::ReadBufferInterface& buffer) override
    {
      ::EmbeddedProto::Error return_value = ::EmbeddedProto::Error::NO_ERRORS;
      ::EmbeddedProto::WireFormatter::WireType wire_type = ::EmbeddedProto::WireFormatter::WireType::VARINT;
      uint32_t id_number = 0;
      FieldNumber id_tag = FieldNumber::NOT_SET;

      ::EmbeddedProto::Error tag_value = ::EmbeddedProto::WireFormatter::DeserializeTag(buffer, wire_type, id_number);
      while((::EmbeddedProto::Error::NO_ERRORS == return_value) && (::EmbeddedProto::Error::NO_ERRORS == tag_value))
      {
        id_tag = static_cast<FieldNumber>(id_number);
        switch(id_tag)
        {
          case FieldNumber::TICK_RELATIVE:
            return_value = tick_relative_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::ADDR_AXON:
            return_value = addr_axon_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::NOT_SET:
            return_value = ::EmbeddedProto::Error::INVALID_FIELD_ID;
            break;

          default:
            return_value = skip_unknown_field(buffer, wire_type);
            break;
        }

        if(::EmbeddedProto::Error::NO_ERRORS == return_value)
        {
          // Read the next tag.
          tag_value = ::EmbeddedProto::WireFormatter::DeserializeTag(buffer, wire_type, id_number);
        }
      }

      // When an error was detect while reading the tag but no other errors where found, set it in the return value.
      if((::EmbeddedProto::Error::NO_ERRORS == return_value)
         && (::EmbeddedProto::Error::NO_ERRORS != tag_value)
         && (::EmbeddedProto::Error::END_OF_BUFFER != tag_value)) // The end of the buffer is not an array in this case.
      {
        return_value = tag_value;
      }

      return return_value;
    };

    void clear() override
    {
      clear_tick_relative();
      clear_addr_axon();

    }

#ifndef DISABLE_FIELD_NUMBER_TO_NAME 

    static char const* field_number_to_name(const FieldNumber fieldNumber)
    {
      char const* name = nullptr;
      switch(fieldNumber)
      {
        case FieldNumber::TICK_RELATIVE:
          name = TICK_RELATIVE_NAME;
          break;
        case FieldNumber::ADDR_AXON:
          name = ADDR_AXON_NAME;
          break;
        default:
          name = "Invalid FieldNumber";
          break;
      }
      return name;
    }

#endif

#ifdef MSG_TO_STRING

    ::EmbeddedProto::string_view to_string(::EmbeddedProto::string_view& str) const
    {
      return this->to_string(str, 0, nullptr, true);
    }

    ::EmbeddedProto::string_view to_string(::EmbeddedProto::string_view& str, const uint32_t indent_level, char const* name, const bool first_field) const override
    {
      ::EmbeddedProto::string_view left_chars = str;
      int32_t n_chars_used = 0;

      if(!first_field)
      {
        // Add a comma behind the previous field.
        n_chars_used = snprintf(left_chars.data, left_chars.size, ",\n");
        if(0 < n_chars_used)
        {
          // Update the character pointer and characters left in the array.
          left_chars.data += n_chars_used;
          left_chars.size -= n_chars_used;
        }
      }

      if(nullptr != name)
      {
        if( 0 == indent_level)
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "\"%s\": {\n", name);
        }
        else
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "%*s\"%s\": {\n", indent_level, " ", name);
        }
      }
      else
      {
        if( 0 == indent_level)
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "{\n");
        }
        else
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "%*s{\n", indent_level, " ");
        }
      }
      
      if(0 < n_chars_used)
      {
        left_chars.data += n_chars_used;
        left_chars.size -= n_chars_used;
      }

      left_chars = tick_relative_.to_string(left_chars, indent_level + 2, TICK_RELATIVE_NAME, true);
      left_chars = addr_axon_.to_string(left_chars, indent_level + 2, ADDR_AXON_NAME, false);
  
      if( 0 == indent_level) 
      {
        n_chars_used = snprintf(left_chars.data, left_chars.size, "\n}");
      }
      else 
      {
        n_chars_used = snprintf(left_chars.data, left_chars.size, "\n%*s}", indent_level, " ");
      }

      if(0 < n_chars_used)
      {
        left_chars.data += n_chars_used;
        left_chars.size -= n_chars_used;
      }

      return left_chars;
    }

#endif // End of MSG_TO_STRING

  private:


      EmbeddedProto::uint32 tick_relative_ = 0U;
      EmbeddedProto::uint32 addr_axon_ = 0U;

};

template<
    uint32_t TestMeta_build_id_LENGTH
>
class TestMeta final: public ::EmbeddedProto::MessageInterface
{
  public:
    TestMeta() = default;
    TestMeta(const TestMeta& rhs )
    {
      set_schema_version(rhs.get_schema_version());
      set_build_id(rhs.get_build_id());
      set_codec(rhs.get_codec());
    }

    TestMeta(const TestMeta&& rhs ) noexcept
    {
      set_schema_version(rhs.get_schema_version());
      set_build_id(rhs.get_build_id());
      set_codec(rhs.get_codec());
    }

    ~TestMeta() override = default;

    enum class FieldNumber : uint32_t
    {
      NOT_SET = 0,
      SCHEMA_VERSION = 1,
      BUILD_ID = 2,
      CODEC = 3
    };

    TestMeta& operator=(const TestMeta& rhs)
    {
      set_schema_version(rhs.get_schema_version());
      set_build_id(rhs.get_build_id());
      set_codec(rhs.get_codec());
      return *this;
    }

    TestMeta& operator=(const TestMeta&& rhs) noexcept
    {
      set_schema_version(rhs.get_schema_version());
      set_build_id(rhs.get_build_id());
      set_codec(rhs.get_codec());
      return *this;
    }

    static constexpr char const* SCHEMA_VERSION_NAME = "schema_version";
    inline void clear_schema_version() { schema_version_.clear(); }
    inline void set_schema_version(const uint32_t& value) { schema_version_ = value; }
    inline void set_schema_version(const uint32_t&& value) { schema_version_ = value; }
    inline uint32_t& mutable_schema_version() { return schema_version_.get(); }
    inline const uint32_t& get_schema_version() const { return schema_version_.get(); }
    inline uint32_t schema_version() const { return schema_version_.get(); }

    static constexpr char const* BUILD_ID_NAME = "build_id";
    inline void clear_build_id() { build_id_.clear(); }
    inline ::EmbeddedProto::FieldString<TestMeta_build_id_LENGTH>& mutable_build_id() { return build_id_; }
    inline void set_build_id(const ::EmbeddedProto::FieldString<TestMeta_build_id_LENGTH>& rhs) { build_id_.set(rhs); }
    inline const ::EmbeddedProto::FieldString<TestMeta_build_id_LENGTH>& get_build_id() const { return build_id_; }
    inline const char* build_id() const { return build_id_.get_const(); }

    static constexpr char const* CODEC_NAME = "codec";
    inline void clear_codec() { codec_.clear(); }
    inline void set_codec(const OutputCodec& value) { codec_ = value; }
    inline void set_codec(const OutputCodec&& value) { codec_ = value; }
    inline const OutputCodec& get_codec() const { return codec_.get(); }
    inline OutputCodec codec() const { return codec_.get(); }


    ::EmbeddedProto::Error serialize(::EmbeddedProto::WriteBufferInterface& buffer) const override
    {
      ::EmbeddedProto::Error return_value = ::EmbeddedProto::Error::NO_ERRORS;

      if((0U != schema_version_.get()) && (::EmbeddedProto::Error::NO_ERRORS == return_value))
      {
        return_value = schema_version_.serialize_with_id(static_cast<uint32_t>(FieldNumber::SCHEMA_VERSION), buffer, false);
      }

      if(::EmbeddedProto::Error::NO_ERRORS == return_value)
      {
        return_value = build_id_.serialize_with_id(static_cast<uint32_t>(FieldNumber::BUILD_ID), buffer, false);
      }

      if((static_cast<OutputCodec>(0) != codec_.get()) && (::EmbeddedProto::Error::NO_ERRORS == return_value))
      {
        return_value = codec_.serialize_with_id(static_cast<uint32_t>(FieldNumber::CODEC), buffer, false);
      }

      return return_value;
    };

    ::EmbeddedProto::Error deserialize(::EmbeddedProto::ReadBufferInterface& buffer) override
    {
      ::EmbeddedProto::Error return_value = ::EmbeddedProto::Error::NO_ERRORS;
      ::EmbeddedProto::WireFormatter::WireType wire_type = ::EmbeddedProto::WireFormatter::WireType::VARINT;
      uint32_t id_number = 0;
      FieldNumber id_tag = FieldNumber::NOT_SET;

      ::EmbeddedProto::Error tag_value = ::EmbeddedProto::WireFormatter::DeserializeTag(buffer, wire_type, id_number);
      while((::EmbeddedProto::Error::NO_ERRORS == return_value) && (::EmbeddedProto::Error::NO_ERRORS == tag_value))
      {
        id_tag = static_cast<FieldNumber>(id_number);
        switch(id_tag)
        {
          case FieldNumber::SCHEMA_VERSION:
            return_value = schema_version_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::BUILD_ID:
            return_value = build_id_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::CODEC:
            return_value = codec_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::NOT_SET:
            return_value = ::EmbeddedProto::Error::INVALID_FIELD_ID;
            break;

          default:
            return_value = skip_unknown_field(buffer, wire_type);
            break;
        }

        if(::EmbeddedProto::Error::NO_ERRORS == return_value)
        {
          // Read the next tag.
          tag_value = ::EmbeddedProto::WireFormatter::DeserializeTag(buffer, wire_type, id_number);
        }
      }

      // When an error was detect while reading the tag but no other errors where found, set it in the return value.
      if((::EmbeddedProto::Error::NO_ERRORS == return_value)
         && (::EmbeddedProto::Error::NO_ERRORS != tag_value)
         && (::EmbeddedProto::Error::END_OF_BUFFER != tag_value)) // The end of the buffer is not an array in this case.
      {
        return_value = tag_value;
      }

      return return_value;
    };

    void clear() override
    {
      clear_schema_version();
      clear_build_id();
      clear_codec();

    }

#ifndef DISABLE_FIELD_NUMBER_TO_NAME 

    static char const* field_number_to_name(const FieldNumber fieldNumber)
    {
      char const* name = nullptr;
      switch(fieldNumber)
      {
        case FieldNumber::SCHEMA_VERSION:
          name = SCHEMA_VERSION_NAME;
          break;
        case FieldNumber::BUILD_ID:
          name = BUILD_ID_NAME;
          break;
        case FieldNumber::CODEC:
          name = CODEC_NAME;
          break;
        default:
          name = "Invalid FieldNumber";
          break;
      }
      return name;
    }

#endif

#ifdef MSG_TO_STRING

    ::EmbeddedProto::string_view to_string(::EmbeddedProto::string_view& str) const
    {
      return this->to_string(str, 0, nullptr, true);
    }

    ::EmbeddedProto::string_view to_string(::EmbeddedProto::string_view& str, const uint32_t indent_level, char const* name, const bool first_field) const override
    {
      ::EmbeddedProto::string_view left_chars = str;
      int32_t n_chars_used = 0;

      if(!first_field)
      {
        // Add a comma behind the previous field.
        n_chars_used = snprintf(left_chars.data, left_chars.size, ",\n");
        if(0 < n_chars_used)
        {
          // Update the character pointer and characters left in the array.
          left_chars.data += n_chars_used;
          left_chars.size -= n_chars_used;
        }
      }

      if(nullptr != name)
      {
        if( 0 == indent_level)
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "\"%s\": {\n", name);
        }
        else
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "%*s\"%s\": {\n", indent_level, " ", name);
        }
      }
      else
      {
        if( 0 == indent_level)
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "{\n");
        }
        else
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "%*s{\n", indent_level, " ");
        }
      }
      
      if(0 < n_chars_used)
      {
        left_chars.data += n_chars_used;
        left_chars.size -= n_chars_used;
      }

      left_chars = schema_version_.to_string(left_chars, indent_level + 2, SCHEMA_VERSION_NAME, true);
      left_chars = build_id_.to_string(left_chars, indent_level + 2, BUILD_ID_NAME, false);
      left_chars = codec_.to_string(left_chars, indent_level + 2, CODEC_NAME, false);
  
      if( 0 == indent_level) 
      {
        n_chars_used = snprintf(left_chars.data, left_chars.size, "\n}");
      }
      else 
      {
        n_chars_used = snprintf(left_chars.data, left_chars.size, "\n%*s}", indent_level, " ");
      }

      if(0 < n_chars_used)
      {
        left_chars.data += n_chars_used;
        left_chars.size -= n_chars_used;
      }

      return left_chars;
    }

#endif // End of MSG_TO_STRING

  private:


      EmbeddedProto::uint32 schema_version_ = 0U;
      ::EmbeddedProto::FieldString<TestMeta_build_id_LENGTH> build_id_;
      EmbeddedProto::enumeration<OutputCodec> codec_ = static_cast<OutputCodec>(0);

};

template<
    uint32_t TestOutput_debug_name_LENGTH, 
    uint32_t TestOutput_shape_REP_LENGTH, 
    uint32_t TestOutput_route_spans_REP_LENGTH
>
class TestOutput final: public ::EmbeddedProto::MessageInterface
{
  public:
    TestOutput() = default;
    TestOutput(const TestOutput& rhs )
    {
      set_output_id(rhs.get_output_id());
      set_debug_name(rhs.get_debug_name());
      set_shape(rhs.get_shape());
      set_route_spans(rhs.get_route_spans());
    }

    TestOutput(const TestOutput&& rhs ) noexcept
    {
      set_output_id(rhs.get_output_id());
      set_debug_name(rhs.get_debug_name());
      set_shape(rhs.get_shape());
      set_route_spans(rhs.get_route_spans());
    }

    ~TestOutput() override = default;

    enum class FieldNumber : uint32_t
    {
      NOT_SET = 0,
      OUTPUT_ID = 1,
      DEBUG_NAME = 2,
      SHAPE = 3,
      ROUTE_SPANS = 4
    };

    TestOutput& operator=(const TestOutput& rhs)
    {
      set_output_id(rhs.get_output_id());
      set_debug_name(rhs.get_debug_name());
      set_shape(rhs.get_shape());
      set_route_spans(rhs.get_route_spans());
      return *this;
    }

    TestOutput& operator=(const TestOutput&& rhs) noexcept
    {
      set_output_id(rhs.get_output_id());
      set_debug_name(rhs.get_debug_name());
      set_shape(rhs.get_shape());
      set_route_spans(rhs.get_route_spans());
      return *this;
    }

    static constexpr char const* OUTPUT_ID_NAME = "output_id";
    inline void clear_output_id() { output_id_.clear(); }
    inline void set_output_id(const uint32_t& value) { output_id_ = value; }
    inline void set_output_id(const uint32_t&& value) { output_id_ = value; }
    inline uint32_t& mutable_output_id() { return output_id_.get(); }
    inline const uint32_t& get_output_id() const { return output_id_.get(); }
    inline uint32_t output_id() const { return output_id_.get(); }

    static constexpr char const* DEBUG_NAME_NAME = "debug_name";
    inline void clear_debug_name() { debug_name_.clear(); }
    inline ::EmbeddedProto::FieldString<TestOutput_debug_name_LENGTH>& mutable_debug_name() { return debug_name_; }
    inline void set_debug_name(const ::EmbeddedProto::FieldString<TestOutput_debug_name_LENGTH>& rhs) { debug_name_.set(rhs); }
    inline const ::EmbeddedProto::FieldString<TestOutput_debug_name_LENGTH>& get_debug_name() const { return debug_name_; }
    inline const char* debug_name() const { return debug_name_.get_const(); }

    static constexpr char const* SHAPE_NAME = "shape";
    inline const EmbeddedProto::uint32& shape(uint32_t index) const { return shape_[index]; }
    inline void clear_shape() { shape_.clear(); }
    inline void set_shape(uint32_t index, const EmbeddedProto::uint32& value) { shape_.set(index, value); }
    inline void set_shape(uint32_t index, const EmbeddedProto::uint32&& value) { shape_.set(index, value); }
    inline void set_shape(const ::EmbeddedProto::RepeatedFieldFixedSize<EmbeddedProto::uint32, TestOutput_shape_REP_LENGTH>& values) { shape_ = values; }
    inline void add_shape(const EmbeddedProto::uint32& value) { shape_.add(value); }
    inline ::EmbeddedProto::RepeatedFieldFixedSize<EmbeddedProto::uint32, TestOutput_shape_REP_LENGTH>& mutable_shape() { return shape_; }
    inline EmbeddedProto::uint32& mutable_shape(uint32_t index) { return shape_[index]; }
    inline const ::EmbeddedProto::RepeatedFieldFixedSize<EmbeddedProto::uint32, TestOutput_shape_REP_LENGTH>& get_shape() const { return shape_; }
    inline const ::EmbeddedProto::RepeatedFieldFixedSize<EmbeddedProto::uint32, TestOutput_shape_REP_LENGTH>& shape() const { return shape_; }

    static constexpr char const* ROUTE_SPANS_NAME = "route_spans";
    inline const RouteSpan& route_spans(uint32_t index) const { return route_spans_[index]; }
    inline void clear_route_spans() { route_spans_.clear(); }
    inline void set_route_spans(uint32_t index, const RouteSpan& value) { route_spans_.set(index, value); }
    inline void set_route_spans(uint32_t index, const RouteSpan&& value) { route_spans_.set(index, value); }
    inline void set_route_spans(const ::EmbeddedProto::RepeatedFieldFixedSize<RouteSpan, TestOutput_route_spans_REP_LENGTH>& values) { route_spans_ = values; }
    inline void add_route_spans(const RouteSpan& value) { route_spans_.add(value); }
    inline ::EmbeddedProto::RepeatedFieldFixedSize<RouteSpan, TestOutput_route_spans_REP_LENGTH>& mutable_route_spans() { return route_spans_; }
    inline RouteSpan& mutable_route_spans(uint32_t index) { return route_spans_[index]; }
    inline const ::EmbeddedProto::RepeatedFieldFixedSize<RouteSpan, TestOutput_route_spans_REP_LENGTH>& get_route_spans() const { return route_spans_; }
    inline const ::EmbeddedProto::RepeatedFieldFixedSize<RouteSpan, TestOutput_route_spans_REP_LENGTH>& route_spans() const { return route_spans_; }


    ::EmbeddedProto::Error serialize(::EmbeddedProto::WriteBufferInterface& buffer) const override
    {
      ::EmbeddedProto::Error return_value = ::EmbeddedProto::Error::NO_ERRORS;

      if((0U != output_id_.get()) && (::EmbeddedProto::Error::NO_ERRORS == return_value))
      {
        return_value = output_id_.serialize_with_id(static_cast<uint32_t>(FieldNumber::OUTPUT_ID), buffer, false);
      }

      if(::EmbeddedProto::Error::NO_ERRORS == return_value)
      {
        return_value = debug_name_.serialize_with_id(static_cast<uint32_t>(FieldNumber::DEBUG_NAME), buffer, false);
      }

      if(::EmbeddedProto::Error::NO_ERRORS == return_value)
      {
        return_value = shape_.serialize_with_id(static_cast<uint32_t>(FieldNumber::SHAPE), buffer, false);
      }

      if(::EmbeddedProto::Error::NO_ERRORS == return_value)
      {
        return_value = route_spans_.serialize_with_id(static_cast<uint32_t>(FieldNumber::ROUTE_SPANS), buffer, false);
      }

      return return_value;
    };

    ::EmbeddedProto::Error deserialize(::EmbeddedProto::ReadBufferInterface& buffer) override
    {
      ::EmbeddedProto::Error return_value = ::EmbeddedProto::Error::NO_ERRORS;
      ::EmbeddedProto::WireFormatter::WireType wire_type = ::EmbeddedProto::WireFormatter::WireType::VARINT;
      uint32_t id_number = 0;
      FieldNumber id_tag = FieldNumber::NOT_SET;

      ::EmbeddedProto::Error tag_value = ::EmbeddedProto::WireFormatter::DeserializeTag(buffer, wire_type, id_number);
      while((::EmbeddedProto::Error::NO_ERRORS == return_value) && (::EmbeddedProto::Error::NO_ERRORS == tag_value))
      {
        id_tag = static_cast<FieldNumber>(id_number);
        switch(id_tag)
        {
          case FieldNumber::OUTPUT_ID:
            return_value = output_id_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::DEBUG_NAME:
            return_value = debug_name_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::SHAPE:
            return_value = shape_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::ROUTE_SPANS:
            return_value = route_spans_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::NOT_SET:
            return_value = ::EmbeddedProto::Error::INVALID_FIELD_ID;
            break;

          default:
            return_value = skip_unknown_field(buffer, wire_type);
            break;
        }

        if(::EmbeddedProto::Error::NO_ERRORS == return_value)
        {
          // Read the next tag.
          tag_value = ::EmbeddedProto::WireFormatter::DeserializeTag(buffer, wire_type, id_number);
        }
      }

      // When an error was detect while reading the tag but no other errors where found, set it in the return value.
      if((::EmbeddedProto::Error::NO_ERRORS == return_value)
         && (::EmbeddedProto::Error::NO_ERRORS != tag_value)
         && (::EmbeddedProto::Error::END_OF_BUFFER != tag_value)) // The end of the buffer is not an array in this case.
      {
        return_value = tag_value;
      }

      return return_value;
    };

    void clear() override
    {
      clear_output_id();
      clear_debug_name();
      clear_shape();
      clear_route_spans();

    }

#ifndef DISABLE_FIELD_NUMBER_TO_NAME 

    static char const* field_number_to_name(const FieldNumber fieldNumber)
    {
      char const* name = nullptr;
      switch(fieldNumber)
      {
        case FieldNumber::OUTPUT_ID:
          name = OUTPUT_ID_NAME;
          break;
        case FieldNumber::DEBUG_NAME:
          name = DEBUG_NAME_NAME;
          break;
        case FieldNumber::SHAPE:
          name = SHAPE_NAME;
          break;
        case FieldNumber::ROUTE_SPANS:
          name = ROUTE_SPANS_NAME;
          break;
        default:
          name = "Invalid FieldNumber";
          break;
      }
      return name;
    }

#endif

#ifdef MSG_TO_STRING

    ::EmbeddedProto::string_view to_string(::EmbeddedProto::string_view& str) const
    {
      return this->to_string(str, 0, nullptr, true);
    }

    ::EmbeddedProto::string_view to_string(::EmbeddedProto::string_view& str, const uint32_t indent_level, char const* name, const bool first_field) const override
    {
      ::EmbeddedProto::string_view left_chars = str;
      int32_t n_chars_used = 0;

      if(!first_field)
      {
        // Add a comma behind the previous field.
        n_chars_used = snprintf(left_chars.data, left_chars.size, ",\n");
        if(0 < n_chars_used)
        {
          // Update the character pointer and characters left in the array.
          left_chars.data += n_chars_used;
          left_chars.size -= n_chars_used;
        }
      }

      if(nullptr != name)
      {
        if( 0 == indent_level)
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "\"%s\": {\n", name);
        }
        else
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "%*s\"%s\": {\n", indent_level, " ", name);
        }
      }
      else
      {
        if( 0 == indent_level)
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "{\n");
        }
        else
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "%*s{\n", indent_level, " ");
        }
      }
      
      if(0 < n_chars_used)
      {
        left_chars.data += n_chars_used;
        left_chars.size -= n_chars_used;
      }

      left_chars = output_id_.to_string(left_chars, indent_level + 2, OUTPUT_ID_NAME, true);
      left_chars = debug_name_.to_string(left_chars, indent_level + 2, DEBUG_NAME_NAME, false);
      left_chars = shape_.to_string(left_chars, indent_level + 2, SHAPE_NAME, false);
      left_chars = route_spans_.to_string(left_chars, indent_level + 2, ROUTE_SPANS_NAME, false);
  
      if( 0 == indent_level) 
      {
        n_chars_used = snprintf(left_chars.data, left_chars.size, "\n}");
      }
      else 
      {
        n_chars_used = snprintf(left_chars.data, left_chars.size, "\n%*s}", indent_level, " ");
      }

      if(0 < n_chars_used)
      {
        left_chars.data += n_chars_used;
        left_chars.size -= n_chars_used;
      }

      return left_chars;
    }

#endif // End of MSG_TO_STRING

  private:


      EmbeddedProto::uint32 output_id_ = 0U;
      ::EmbeddedProto::FieldString<TestOutput_debug_name_LENGTH> debug_name_;
      ::EmbeddedProto::RepeatedFieldFixedSize<EmbeddedProto::uint32, TestOutput_shape_REP_LENGTH> shape_;
      ::EmbeddedProto::RepeatedFieldFixedSize<RouteSpan, TestOutput_route_spans_REP_LENGTH> route_spans_;

};

template<
    uint32_t TestMsg_meta_TestMeta_build_id_LENGTH, 
    uint32_t TestMsg_input_shape_REP_LENGTH, 
    uint32_t TestMsg_outputs_REP_LENGTH, 
    uint32_t TestMsg_outputs_TestOutput_debug_name_LENGTH, 
    uint32_t TestMsg_outputs_TestOutput_shape_REP_LENGTH, 
    uint32_t TestMsg_outputs_TestOutput_route_spans_REP_LENGTH
>
class TestMsg final: public ::EmbeddedProto::MessageInterface
{
  public:
    TestMsg() = default;
    TestMsg(const TestMsg& rhs )
    {
      set_meta(rhs.get_meta());
      set_enabled(rhs.get_enabled());
      set_offset(rhs.get_offset());
      set_input_shape(rhs.get_input_shape());
      set_outputs(rhs.get_outputs());
    }

    TestMsg(const TestMsg&& rhs ) noexcept
    {
      set_meta(rhs.get_meta());
      set_enabled(rhs.get_enabled());
      set_offset(rhs.get_offset());
      set_input_shape(rhs.get_input_shape());
      set_outputs(rhs.get_outputs());
    }

    ~TestMsg() override = default;

    enum class FieldNumber : uint32_t
    {
      NOT_SET = 0,
      META = 1,
      ENABLED = 2,
      OFFSET = 3,
      INPUT_SHAPE = 4,
      OUTPUTS = 5
    };

    TestMsg& operator=(const TestMsg& rhs)
    {
      set_meta(rhs.get_meta());
      set_enabled(rhs.get_enabled());
      set_offset(rhs.get_offset());
      set_input_shape(rhs.get_input_shape());
      set_outputs(rhs.get_outputs());
      return *this;
    }

    TestMsg& operator=(const TestMsg&& rhs) noexcept
    {
      set_meta(rhs.get_meta());
      set_enabled(rhs.get_enabled());
      set_offset(rhs.get_offset());
      set_input_shape(rhs.get_input_shape());
      set_outputs(rhs.get_outputs());
      return *this;
    }

    static constexpr char const* META_NAME = "meta";
    inline void clear_meta() { meta_.clear(); }
    inline void set_meta(const TestMeta<TestMsg_meta_TestMeta_build_id_LENGTH>& value) { meta_ = value; }
    inline void set_meta(const TestMeta<TestMsg_meta_TestMeta_build_id_LENGTH>&& value) { meta_ = value; }
    inline TestMeta<TestMsg_meta_TestMeta_build_id_LENGTH>& mutable_meta() { return meta_; }
    inline const TestMeta<TestMsg_meta_TestMeta_build_id_LENGTH>& get_meta() const { return meta_; }
    inline const TestMeta<TestMsg_meta_TestMeta_build_id_LENGTH>& meta() const { return meta_; }

    static constexpr char const* ENABLED_NAME = "enabled";
    inline void clear_enabled() { enabled_.clear(); }
    inline void set_enabled(const bool& value) { enabled_ = value; }
    inline void set_enabled(const bool&& value) { enabled_ = value; }
    inline bool& mutable_enabled() { return enabled_.get(); }
    inline const bool& get_enabled() const { return enabled_.get(); }
    inline bool enabled() const { return enabled_.get(); }

    static constexpr char const* OFFSET_NAME = "offset";
    inline void clear_offset() { offset_.clear(); }
    inline void set_offset(const int32_t& value) { offset_ = value; }
    inline void set_offset(const int32_t&& value) { offset_ = value; }
    inline int32_t& mutable_offset() { return offset_.get(); }
    inline const int32_t& get_offset() const { return offset_.get(); }
    inline int32_t offset() const { return offset_.get(); }

    static constexpr char const* INPUT_SHAPE_NAME = "input_shape";
    inline const EmbeddedProto::uint32& input_shape(uint32_t index) const { return input_shape_[index]; }
    inline void clear_input_shape() { input_shape_.clear(); }
    inline void set_input_shape(uint32_t index, const EmbeddedProto::uint32& value) { input_shape_.set(index, value); }
    inline void set_input_shape(uint32_t index, const EmbeddedProto::uint32&& value) { input_shape_.set(index, value); }
    inline void set_input_shape(const ::EmbeddedProto::RepeatedFieldFixedSize<EmbeddedProto::uint32, TestMsg_input_shape_REP_LENGTH>& values) { input_shape_ = values; }
    inline void add_input_shape(const EmbeddedProto::uint32& value) { input_shape_.add(value); }
    inline ::EmbeddedProto::RepeatedFieldFixedSize<EmbeddedProto::uint32, TestMsg_input_shape_REP_LENGTH>& mutable_input_shape() { return input_shape_; }
    inline EmbeddedProto::uint32& mutable_input_shape(uint32_t index) { return input_shape_[index]; }
    inline const ::EmbeddedProto::RepeatedFieldFixedSize<EmbeddedProto::uint32, TestMsg_input_shape_REP_LENGTH>& get_input_shape() const { return input_shape_; }
    inline const ::EmbeddedProto::RepeatedFieldFixedSize<EmbeddedProto::uint32, TestMsg_input_shape_REP_LENGTH>& input_shape() const { return input_shape_; }

    static constexpr char const* OUTPUTS_NAME = "outputs";
    inline const TestOutput<TestMsg_outputs_TestOutput_debug_name_LENGTH, TestMsg_outputs_TestOutput_shape_REP_LENGTH, TestMsg_outputs_TestOutput_route_spans_REP_LENGTH>& outputs(uint32_t index) const { return outputs_[index]; }
    inline void clear_outputs() { outputs_.clear(); }
    inline void set_outputs(uint32_t index, const TestOutput<TestMsg_outputs_TestOutput_debug_name_LENGTH, TestMsg_outputs_TestOutput_shape_REP_LENGTH, TestMsg_outputs_TestOutput_route_spans_REP_LENGTH>& value) { outputs_.set(index, value); }
    inline void set_outputs(uint32_t index, const TestOutput<TestMsg_outputs_TestOutput_debug_name_LENGTH, TestMsg_outputs_TestOutput_shape_REP_LENGTH, TestMsg_outputs_TestOutput_route_spans_REP_LENGTH>&& value) { outputs_.set(index, value); }
    inline void set_outputs(const ::EmbeddedProto::RepeatedFieldFixedSize<TestOutput<TestMsg_outputs_TestOutput_debug_name_LENGTH, TestMsg_outputs_TestOutput_shape_REP_LENGTH, TestMsg_outputs_TestOutput_route_spans_REP_LENGTH>, TestMsg_outputs_REP_LENGTH>& values) { outputs_ = values; }
    inline void add_outputs(const TestOutput<TestMsg_outputs_TestOutput_debug_name_LENGTH, TestMsg_outputs_TestOutput_shape_REP_LENGTH, TestMsg_outputs_TestOutput_route_spans_REP_LENGTH>& value) { outputs_.add(value); }
    inline ::EmbeddedProto::RepeatedFieldFixedSize<TestOutput<TestMsg_outputs_TestOutput_debug_name_LENGTH, TestMsg_outputs_TestOutput_shape_REP_LENGTH, TestMsg_outputs_TestOutput_route_spans_REP_LENGTH>, TestMsg_outputs_REP_LENGTH>& mutable_outputs() { return outputs_; }
    inline TestOutput<TestMsg_outputs_TestOutput_debug_name_LENGTH, TestMsg_outputs_TestOutput_shape_REP_LENGTH, TestMsg_outputs_TestOutput_route_spans_REP_LENGTH>& mutable_outputs(uint32_t index) { return outputs_[index]; }
    inline const ::EmbeddedProto::RepeatedFieldFixedSize<TestOutput<TestMsg_outputs_TestOutput_debug_name_LENGTH, TestMsg_outputs_TestOutput_shape_REP_LENGTH, TestMsg_outputs_TestOutput_route_spans_REP_LENGTH>, TestMsg_outputs_REP_LENGTH>& get_outputs() const { return outputs_; }
    inline const ::EmbeddedProto::RepeatedFieldFixedSize<TestOutput<TestMsg_outputs_TestOutput_debug_name_LENGTH, TestMsg_outputs_TestOutput_shape_REP_LENGTH, TestMsg_outputs_TestOutput_route_spans_REP_LENGTH>, TestMsg_outputs_REP_LENGTH>& outputs() const { return outputs_; }


    ::EmbeddedProto::Error serialize(::EmbeddedProto::WriteBufferInterface& buffer) const override
    {
      ::EmbeddedProto::Error return_value = ::EmbeddedProto::Error::NO_ERRORS;

      if(::EmbeddedProto::Error::NO_ERRORS == return_value)
      {
        return_value = meta_.serialize_with_id(static_cast<uint32_t>(FieldNumber::META), buffer, false);
      }

      if((false != enabled_.get()) && (::EmbeddedProto::Error::NO_ERRORS == return_value))
      {
        return_value = enabled_.serialize_with_id(static_cast<uint32_t>(FieldNumber::ENABLED), buffer, false);
      }

      if((0 != offset_.get()) && (::EmbeddedProto::Error::NO_ERRORS == return_value))
      {
        return_value = offset_.serialize_with_id(static_cast<uint32_t>(FieldNumber::OFFSET), buffer, false);
      }

      if(::EmbeddedProto::Error::NO_ERRORS == return_value)
      {
        return_value = input_shape_.serialize_with_id(static_cast<uint32_t>(FieldNumber::INPUT_SHAPE), buffer, false);
      }

      if(::EmbeddedProto::Error::NO_ERRORS == return_value)
      {
        return_value = outputs_.serialize_with_id(static_cast<uint32_t>(FieldNumber::OUTPUTS), buffer, false);
      }

      return return_value;
    };

    ::EmbeddedProto::Error deserialize(::EmbeddedProto::ReadBufferInterface& buffer) override
    {
      ::EmbeddedProto::Error return_value = ::EmbeddedProto::Error::NO_ERRORS;
      ::EmbeddedProto::WireFormatter::WireType wire_type = ::EmbeddedProto::WireFormatter::WireType::VARINT;
      uint32_t id_number = 0;
      FieldNumber id_tag = FieldNumber::NOT_SET;

      ::EmbeddedProto::Error tag_value = ::EmbeddedProto::WireFormatter::DeserializeTag(buffer, wire_type, id_number);
      while((::EmbeddedProto::Error::NO_ERRORS == return_value) && (::EmbeddedProto::Error::NO_ERRORS == tag_value))
      {
        id_tag = static_cast<FieldNumber>(id_number);
        switch(id_tag)
        {
          case FieldNumber::META:
            return_value = meta_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::ENABLED:
            return_value = enabled_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::OFFSET:
            return_value = offset_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::INPUT_SHAPE:
            return_value = input_shape_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::OUTPUTS:
            return_value = outputs_.deserialize_check_type(buffer, wire_type);
            break;

          case FieldNumber::NOT_SET:
            return_value = ::EmbeddedProto::Error::INVALID_FIELD_ID;
            break;

          default:
            return_value = skip_unknown_field(buffer, wire_type);
            break;
        }

        if(::EmbeddedProto::Error::NO_ERRORS == return_value)
        {
          // Read the next tag.
          tag_value = ::EmbeddedProto::WireFormatter::DeserializeTag(buffer, wire_type, id_number);
        }
      }

      // When an error was detect while reading the tag but no other errors where found, set it in the return value.
      if((::EmbeddedProto::Error::NO_ERRORS == return_value)
         && (::EmbeddedProto::Error::NO_ERRORS != tag_value)
         && (::EmbeddedProto::Error::END_OF_BUFFER != tag_value)) // The end of the buffer is not an array in this case.
      {
        return_value = tag_value;
      }

      return return_value;
    };

    void clear() override
    {
      clear_meta();
      clear_enabled();
      clear_offset();
      clear_input_shape();
      clear_outputs();

    }

#ifndef DISABLE_FIELD_NUMBER_TO_NAME 

    static char const* field_number_to_name(const FieldNumber fieldNumber)
    {
      char const* name = nullptr;
      switch(fieldNumber)
      {
        case FieldNumber::META:
          name = META_NAME;
          break;
        case FieldNumber::ENABLED:
          name = ENABLED_NAME;
          break;
        case FieldNumber::OFFSET:
          name = OFFSET_NAME;
          break;
        case FieldNumber::INPUT_SHAPE:
          name = INPUT_SHAPE_NAME;
          break;
        case FieldNumber::OUTPUTS:
          name = OUTPUTS_NAME;
          break;
        default:
          name = "Invalid FieldNumber";
          break;
      }
      return name;
    }

#endif

#ifdef MSG_TO_STRING

    ::EmbeddedProto::string_view to_string(::EmbeddedProto::string_view& str) const
    {
      return this->to_string(str, 0, nullptr, true);
    }

    ::EmbeddedProto::string_view to_string(::EmbeddedProto::string_view& str, const uint32_t indent_level, char const* name, const bool first_field) const override
    {
      ::EmbeddedProto::string_view left_chars = str;
      int32_t n_chars_used = 0;

      if(!first_field)
      {
        // Add a comma behind the previous field.
        n_chars_used = snprintf(left_chars.data, left_chars.size, ",\n");
        if(0 < n_chars_used)
        {
          // Update the character pointer and characters left in the array.
          left_chars.data += n_chars_used;
          left_chars.size -= n_chars_used;
        }
      }

      if(nullptr != name)
      {
        if( 0 == indent_level)
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "\"%s\": {\n", name);
        }
        else
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "%*s\"%s\": {\n", indent_level, " ", name);
        }
      }
      else
      {
        if( 0 == indent_level)
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "{\n");
        }
        else
        {
          n_chars_used = snprintf(left_chars.data, left_chars.size, "%*s{\n", indent_level, " ");
        }
      }
      
      if(0 < n_chars_used)
      {
        left_chars.data += n_chars_used;
        left_chars.size -= n_chars_used;
      }

      left_chars = meta_.to_string(left_chars, indent_level + 2, META_NAME, true);
      left_chars = enabled_.to_string(left_chars, indent_level + 2, ENABLED_NAME, false);
      left_chars = offset_.to_string(left_chars, indent_level + 2, OFFSET_NAME, false);
      left_chars = input_shape_.to_string(left_chars, indent_level + 2, INPUT_SHAPE_NAME, false);
      left_chars = outputs_.to_string(left_chars, indent_level + 2, OUTPUTS_NAME, false);
  
      if( 0 == indent_level) 
      {
        n_chars_used = snprintf(left_chars.data, left_chars.size, "\n}");
      }
      else 
      {
        n_chars_used = snprintf(left_chars.data, left_chars.size, "\n%*s}", indent_level, " ");
      }

      if(0 < n_chars_used)
      {
        left_chars.data += n_chars_used;
        left_chars.size -= n_chars_used;
      }

      return left_chars;
    }

#endif // End of MSG_TO_STRING

  private:


      TestMeta<TestMsg_meta_TestMeta_build_id_LENGTH> meta_;
      EmbeddedProto::boolean enabled_ = false;
      EmbeddedProto::sint32 offset_ = 0;
      ::EmbeddedProto::RepeatedFieldFixedSize<EmbeddedProto::uint32, TestMsg_input_shape_REP_LENGTH> input_shape_;
      ::EmbeddedProto::RepeatedFieldFixedSize<TestOutput<TestMsg_outputs_TestOutput_debug_name_LENGTH, TestMsg_outputs_TestOutput_shape_REP_LENGTH, TestMsg_outputs_TestOutput_route_spans_REP_LENGTH>, TestMsg_outputs_REP_LENGTH> outputs_;

};

#endif // TEST_H