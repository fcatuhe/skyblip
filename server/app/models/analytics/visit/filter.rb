module Analytics
  class Visit
    class Filter
      def initialize(filter_params)
        @filter_params = filter_params
      end

      def scope
        apply_filters
      end

      private
        attr_reader :filter_params

        def apply_filters
          scope = apply_analytics_filters
          apply_ahoy_filters(scope)
        end

        def apply_ahoy_filters(scope)
          ahoy_filters = filter_params.slice(*AHOY_VISIT_FILTER_KEYS)

          if ahoy_filters.present?
            scope.merge(Ahoy::Visit.filter_with(ahoy_filters))
          else
            scope
          end
        end

        def apply_analytics_filters
          scope = apply_unknown_filters
          filters = filter_params.slice(*ANALYTICS_VISIT_FILTER_KEYS).except(*unknown_keys)
          scope = scope.filter_with(filters) if filters.present?
          scope
        end

        def apply_unknown_filters
          scope = Visit.joins(:ahoy_visit)
          scope = scope.where(entry_path: [ nil, "" ]) if filter_params[:entry] == "unknown"
          scope = scope.where(exit_path: [ nil, "" ]) if filter_params[:exit] == "unknown"

          if filter_params[:path] == "unknown"
            scope = scope.where(<<~SQL.squish)
              analytics_visits.page_paths IS NULL
              OR analytics_visits.page_paths = '[]'
              OR json_array_length(analytics_visits.page_paths) = 0
            SQL
          end

          scope
        end

        def unknown_keys
          @unknown_keys ||= %i[entry exit path].select { |key| filter_params[key] == "unknown" }
        end
    end
  end
end
