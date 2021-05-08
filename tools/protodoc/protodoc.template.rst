{% macro format_header(header, underline="=") -%}
{{header}}
{{underline * header|length}}

{% endmacro -%}

{% macro format_extension(extension) -%}
.. _extension_{{extension["name"]}}:

This extension may be referenced by the qualified name ``{{extension["name"]}}``

.. note::
  {{extension["status"]}}

  {{extension["security_posture"]}}

.. tip::
  This extension extends and can be used with the following extension {% if extension["categories"]|length > 1 %}categories{% else %}category{% endif %}:

{% for cat in extension["categories"] %}
  - :ref:`{{cat}} <extension_category_{{cat}}>`
{% endfor %}
{% endmacro -%}

{% macro format_enum(enum) -%}
.. _{{enum["anchor"]}}:

{{format_header(enum["header"], "-")}}
{{enum["proto_link"]}}
{{enum["formatted_leading_comment"]}}
{{enum["dl_enum"]}}
{% endmacro -%}

{% macro format_message(msg) -%}
.. _{{msg["anchor"]}}:

{{format_header(msg["header"], "-")}}
{{msg["proto_link"]}}
{{msg["formatted_leading_comment"]}}
{{msg["json_message"]}}
{% for field in msg["fields"] %}
{{field["anchor"]}}
{{field["name"]}}
{% for line in field["comment"] %}
  {{line}}
{% endfor %}
{{field["security_options"]}}
{% endfor %}

{% for nested_msg in msg["messages"] -%}
{{format_message(nested_msg)}}
{% endfor %}

{% for nested_enum in msg["enums"] -%}
{{format_enum(nested_enum)}}
{% endfor %}
{% endmacro -%}

.. _{{file_anchor}}:

{% if not has_messages %}
:orphan:

{% endif %}
{% if file_header %}
{{format_header(file_header)}}
{% endif %}
{% if file_extension %}
{{format_extension(file_extension)}}
{% endif %}
{{file_comments}}

{% if v2_link %}
This documentation is for the Envoy v3 API.

As of Envoy v1.18 the v2 API has been removed and is no longer supported.

If you are upgrading from v2 API config you may wish to view the v2 API documentation:

    :ref:`{{v2_link["text"]}} <{{v2_link["url"]}}>`

{% endif %}

{% if work_in_progress %}
'.. warning::
  This API is work-in-progress and is subject to breaking changes.

{% endif %}

{% for msg in msgs %}
{{format_message(msg)}}
{% endfor %}

{% for enum in enums %}
{{format_enum(enum)}}
{% endfor %}
