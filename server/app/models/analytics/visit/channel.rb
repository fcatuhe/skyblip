module Analytics
  class Visit
    class Channel
      SEARCH_DOMAINS = %w[
        google. bing. yahoo. duckduckgo. yandex. ecosia. qwant.
      ].freeze

      SOCIAL_DOMAINS = %w[
        facebook.com m.facebook.com instagram.com linkedin.com lnkd.in twitter.com x.com t.co
        reddit.com tiktok.com pinterest.com
      ].freeze

      EMAIL_DOMAIN_PREFIXES = %w[
        mail. gmail. hotmail. outlook. proton. protonmail. hey.
      ].freeze

      def initialize(ahoy_visit)
        @referring_domain = ahoy_visit.referring_domain
      end

      def name
        if direct?
          :direct
        elsif email?
          :email
        elsif search?
          :search
        elsif social?
          :social
        else
          :referral
        end
      end

      private

        def direct?
          resolved_domain.blank?
        end

        def email?
          EMAIL_DOMAIN_PREFIXES.any? { |prefix| resolved_domain.start_with?(prefix) }
        end

        def search?
          matches_any?(SEARCH_DOMAINS)
        end

        def social?
          matches_any?(SOCIAL_DOMAINS)
        end

        def matches_any?(patterns)
          patterns.any? { |pattern| resolved_domain.include?(pattern) }
        end

        def resolved_domain
          @resolved_domain ||= @referring_domain.to_s.downcase
        end
    end
  end
end
